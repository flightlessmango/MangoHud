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

    resources = std::move(next);
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

            if (frame_queue.empty())
                return;

            frame = std::move(frame_queue.front());
            frame_queue.pop_front();
        }

        process_frame(std::move(frame));
    }
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

    unique_fd fd;
    const auto method = resources->method;
    auto submit_vulkan = [&](ExportMethod method) {
        if (!resources->vk)
            return false;

        const bool use_opaque = method == OPAQUE_VULKAN;
        Resolution size{w, h};
        if (!resources->vk->submit(resources->buffers, w, h, size, frame.idx,
                                   use_opaque, hud, hud_m)) {
            if (!resize(size, method))
                apply_method_or_next(method);

            return false;
        }

        fd = unique_fd::adopt(resources->vk->get_fence_fd(resources->buffers[frame.idx].sync.fence));
        if (fd && frame.idx < std::ssize(resources->render_fences))
            resources->render_fences[frame.idx] = unique_fd::dup(fd.get());

        return true;
    };

    if (method == DMABUF_VULKAN || method == OPAQUE_VULKAN) {
        if (!submit_vulkan(method))
            return;
    } else if (method == DMABUF_EGL) {
        if (!resources->egl)
            return;

        Resolution size{w, h};
        int fence_fd = resources->egl->submit(resources->buffers, w, h, size, frame.idx, hud, hud_m);
        if (fence_fd < 0) {
            if (resize(size, method))
                return;

            apply_method_or_next(method);
            return;
        }

        fd = unique_fd::adopt(fence_fd);
    }

    if (fd)
        client->frame_ready(frame.idx, std::move(fd), resources);
}
