#include "renderer.h"

#include <drm/drm_fourcc.h>

#include "egl_ctx.h"
#include "shared.h"
#include "vulkan_ctx.h"
#include "../ipc/client.h"
#include "../server/server.h"

#include <cerrno>
#include <iterator>
#include <mutex>
#include <pthread.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <utility>

static bool wait_sync_fd(int fd, const std::atomic<bool>& stop)
{
    if (fd < 0)
        return false;

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!stop.load(std::memory_order_acquire)) {
        pfd.revents = 0;
        int ret = poll(&pfd, 1, 100);
        if (ret == 1)
            return (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        if (ret == 0)
            continue;
        if (errno == EINTR)
            continue;

        SPDLOG_ERROR("renderer: sync fd wait failed errno={}", errno);
        return false;
    }

    return false;
}

Renderer::Resources::Resources(MangoHudServer* server_, ExportMethod method_)
    : server(server_), method(method_) {}

bool Renderer::Resources::init_method(int64_t render_minor, int buffer_size, uint32_t w, uint32_t h)
{
    width = w;
    height = h;
    auto size = buffer_size > 0 ? static_cast<size_t>(buffer_size) : 0;
    if (!server)
        return false;

    auto init_vulkan = [&](bool opaque) {
        vk = server->vk(render_minor);
        if (!vk)
            return false;

        if (!vk->init_client(buffers, cmd_pool, w, h, size, opaque))
            return false;

        vk->init_imgui();
        return true;
    };

    bool initialized = false;
    if (method == DMABUF_VULKAN || method == OPAQUE_VULKAN) {
        initialized = init_vulkan(method == OPAQUE_VULKAN);
    } else if (method == DMABUF_EGL) {
        egl = server->egl(render_minor);
        if (!egl)
            return false;

        initialized = egl->init_client(buffers, w, h, buffer_size);
    }

    if (initialized)
        render_fences.resize(buffers.size());

    return initialized;
}

Renderer::Resources::~Resources()
{
    if (vk && vk->device) {
        std::lock_guard lock(vk->m);
        vkDeviceWaitIdle(vk->device);

        for (auto& buf : buffers) {
            destroy_vk_images(vk->device, buf.dmabuf.image_res);
            destroy_vk_images(vk->device, buf.opaque.image_res);
            destroy_vk_images(vk->device, buf.source.image_res);

            buf.dmabuf.gbm = {};

            if (buf.sync.fence) {
                vkDestroyFence(vk->device, buf.sync.fence, nullptr);
                buf.sync.fence = VK_NULL_HANDLE;
            }

            if (buf.sync.cmd && cmd_pool) {
                vkFreeCommandBuffers(vk->device, cmd_pool, 1, &buf.sync.cmd);
                buf.sync.cmd = VK_NULL_HANDLE;
            }
        }

        if (cmd_pool) {
            vkDestroyCommandPool(vk->device, cmd_pool, nullptr);
            cmd_pool = VK_NULL_HANDLE;
        }
    }

    if (egl)
        egl->destroy_client(buffers);
}

Renderer::Renderer(MangoHudServer* server_, Client* client_, int64_t render_minor_, int buffer_size_)
    : server(server_),
      client(client_),
      render_minor(render_minor_),
      buffer_size(buffer_size_)
{
    hidden = server->config->hide_hud.load();
    apply_method_or_next(default_method);
    thread = std::thread([this] { run(); });
}

Renderer::~Renderer()
{
    stop.store(true, std::memory_order_release);
    frame_cv.notify_all();

    if (thread.joinable())
        thread.join();
}

bool Renderer::method_failed()
{
    std::lock_guard lock(m);

    if (!resources || resources->method >= COUNT)
        return false;

    auto next = static_cast<int32_t>(resources->method) + 1;
    if (next >= COUNT)
        return false;

    return apply_method_or_next(static_cast<ExportMethod>(next));
}

void Renderer::frame_ready(int idx, unique_fd fd, std::shared_ptr<Resources> source)
{
    if (idx < 0)
        return;

    std::lock_guard resources_lock(m);
    if (!resources || idx >= std::ssize(resources->buffers) || (source && source != resources))
        return;
    if (hidden) {
        auto& ready = resources->render_fences[idx];
        if (ready)
            client->frame_ready(idx, std::move(ready), resources);
        return;
    }
    if (!source)
        source = resources;
    {
        std::lock_guard lock(frame_m);
        frame_queue.push_back({{idx, std::move(fd)}, std::move(source)});
    }
    frame_cv.notify_one();
}

void Renderer::set_hud(HudConfig next)
{
    std::lock_guard lock(hud_m);
    *hud = std::move(next);
}

void Renderer::set_hidden(bool next)
{
    std::lock_guard lock(m);
    if (hidden == next)
        return;
    hidden = next;
    apply_method_or_next(resources ? resources->method : default_method);
}

std::shared_ptr<Renderer::Resources> Renderer::get_resources()
{
    std::lock_guard lock(m);
    return resources;
}

bool Renderer::apply_method(ExportMethod method)
{
    resources.reset();
    auto next = std::make_shared<Resources>(server, method);

    if (!next->init_method(render_minor, buffer_size, w, h)) {
        SPDLOG_ERROR("renderer: failed to initialize export method {}", static_cast<int32_t>(method));
        return false;
    }

    {
        std::lock_guard lock(frame_m);
        frame_queue.clear();
    }
    resources = std::move(next);
    if (hidden && !hide_hud())
        return false;
    send_dmabuf();
    return true;
}

bool Renderer::apply_method_or_next(ExportMethod method)
{
    for (int32_t next = static_cast<int32_t>(method); next < static_cast<int32_t>(COUNT); next++) {
        if (apply_method(static_cast<ExportMethod>(next)))
            return true;
    }

    SPDLOG_ERROR("renderer: no usable export method");
    resources.reset();
    return false;
}

bool Renderer::resize(const Resolution& size, ExportMethod method)
{
    const Resolution current{w, h};
    if (size == current)
        return false;

    SPDLOG_DEBUG("renderer: resizing image from: {} {} to {} {}",
                 w, h, size.w, size.h);
    w = size.w;
    h = size.h;
    apply_method_or_next(method);
    return true;
}

void Renderer::send_dmabuf()
{
    if (!client || !resources)
        return;

    client->send_dmabuf(resources->buffers, w, h, resources->method);
}

void Renderer::run()
{
    pthread_setname_np(pthread_self(), "mhud renderer");

    while (!stop.load(std::memory_order_acquire)) {
        QueuedFrame frame;
        {
            std::unique_lock lock(frame_m);
            frame_cv.wait(lock, [this] {
                return stop.load(std::memory_order_acquire) || !frame_queue.empty();
            });

            if (stop.load(std::memory_order_acquire))
                return;
            if (frame_queue.empty())
                continue;

            frame = std::move(frame_queue.front());
            frame_queue.pop_front();
        }

        process_frame(std::move(frame));
    }
}

bool Renderer::hide_hud()
{
    auto blank = std::make_shared<HudConfig>();
    blank->windows.clear();
    for (int idx = 0; idx < std::ssize(resources->buffers); ++idx) {
        Resolution size{w, h};
        auto fd = render_frame(idx, size, blank);
        if (!fd)
            return false;
        resources->render_fences[idx] = std::move(fd);
    }
    return true;
}

unique_fd Renderer::render_frame(int idx, Resolution& size, std::shared_ptr<HudConfig> frame_hud)
{
    const auto method = resources->method;
    if (method == DMABUF_VULKAN || method == OPAQUE_VULKAN) {
        if (resources->vk && resources->vk->submit(resources->buffers, w, h, size, idx,
                                                  method == OPAQUE_VULKAN, frame_hud, hud_m))
            return unique_fd::adopt(resources->vk->get_fence_fd(resources->buffers[idx].sync.fence));
    } else if (method == DMABUF_EGL && resources->egl) {
        return unique_fd::adopt(resources->egl->submit(resources->buffers, w, h, size, idx,
                                                       frame_hud, hud_m));
    }
    return {};
}

void Renderer::process_frame(QueuedFrame queued)
{
    auto& frame = queued.frame;
    if (frame.idx < 0)
        return;

    if (!wait_sync_fd(frame.fd.get(), stop)) {
        SPDLOG_DEBUG("renderer: skipped frame {} because input fence did not signal", frame.idx);
        return;
    }

    std::lock_guard lock(m);
    if (queued.source && queued.source != resources)
        return;
    if (!resources || frame.idx >= std::ssize(resources->buffers))
        return;

    if (frame.idx < std::ssize(resources->render_fences) &&
        resources->render_fences[frame.idx]) {
        if (!wait_sync_fd(resources->render_fences[frame.idx].get(), stop)) {
            SPDLOG_DEBUG("renderer: skipped frame {} because render fence did not signal", frame.idx);
            return;
        }
        resources->render_fences[frame.idx] = {};
    }

    const auto method = resources->method;
    Resolution size{w, h};
    auto fd = render_frame(frame.idx, size, hud);
    if (!fd) {
        if (!resize(size, method))
            apply_method_or_next(method);
        return;
    }
    if (resources->vk)
        resources->render_fences[frame.idx] = unique_fd::dup(fd.get());
    client->frame_ready(frame.idx, std::move(fd), resources);
}
