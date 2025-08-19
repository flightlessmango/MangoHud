#include "lsfg-vk.h"
#include "fps_metrics.h"
#include "hud_elements.h"
#include "overlay.h"
#include "mesa/util/macros.h"
#include <link.h>
#include "fps_metrics.h"

void* lsfg_vk::lsfg_handle = nullptr;

lsfg_vk::lsfg_vk() {
    lsfg_handle = dlopen("liblsfg-vk.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lsfg_handle)
        lsfg_handle = find_loaded_lsfg_lib();

    if (!lsfg_handle)
        SPDLOG_DEBUG("Failed to locate file lsfg-vk library");

    using get_conf_fn = Configuration* (*)();
    auto get_conf = reinterpret_cast<get_conf_fn>(dlsym(lsfg_handle, "get_active_conf"));

    if (get_conf)
        conf = get_conf();


    if (conf->enable)
        inited = true;
}

void lsfg_vk::notify_extra_frame(uint64_t n_frames) {
    swapchain_stats* sw_stats = HUDElements.sw_stats;
    
    if (!sw_stats)
        return;

    sw_stats->engine = EngineTypes::LSFG;
    mangohud_is_receiving_all_lsfg_frames = sw_stats->n_frames == n_frames;
    uint64_t now = os_time_get_nano();
    uint64_t frametime_ns = now - sw_stats->last_present_time;
    float frametime_ms = frametime_ns / 1000000.f;
    static uint64_t lsfg_frames = 0;
    uint32_t f_idx = lsfg_frames % ARRAY_SIZE(frametimes);
    frametimes[f_idx] = frametime_ms;

    if (!mangohud_is_receiving_all_lsfg_frames) {
        add_frame_to_combined(frametime_ms);
        if (fpsmetrics)
            fpsmetrics->update(frametime_ms);
    }

    lsfg_frames++;
}

bool lsfg_vk::is_active() {
    return inited && conf && conf->enable;
}

void lsfg_vk::add_frame_to_combined(float ms) {
    std::lock_guard<std::mutex> lock(combined_lock);
    memmove(&combined_frametimes[0],
                 &combined_frametimes[1],
                 (ARRAY_SIZE(combined_frametimes) - 1) * sizeof(float));

    combined_frametimes[ARRAY_SIZE(combined_frametimes) - 1] = ms;
}

int lsfg_vk::find_lsfg_callback(struct dl_phdr_info* info, size_t size, void* data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "liblsfg-vk.so")) {
        void* handle = dlopen(info->dlpi_name, RTLD_NOW | RTLD_NOLOAD);
        if (handle) {
            lsfg_handle = handle;
            return 1;
        }
    }
    return 0;
}

void* lsfg_vk::find_loaded_lsfg_lib() {
    dl_iterate_phdr(find_lsfg_callback, nullptr);
    if (lsfg_handle)
        return lsfg_handle;

    return nullptr;
}

float lsfg_vk::current_fps() {
    float sum = 0.0f;
    for (float ft : frametimes)
        sum += ft;

    if (sum > 0)
        return 1000.f / (sum / ARRAY_SIZE(frametimes));

    return 0;
}

float lsfg_vk::min_frametime() {
    float frametime_min = std::numeric_limits<float>::max();

    for (float ft : combined_frametimes)
        if (ft > 0.f)
            frametime_min = MIN2(frametime_min, ft);

    return frametime_min;
}

float lsfg_vk::max_frametime() {
    float frametime_max = 0.f;
    for (float ft : combined_frametimes)
        frametime_max = MAX2(frametime_max, ft);

    return frametime_max;
}

extern "C" __attribute__((visibility("default"))) __attribute__((used))
void notify_extra_frame(uint64_t n_frames) {
    if (!lsfg_ptr)
        lsfg_ptr = std::make_unique<lsfg_vk>();

    lsfg_ptr->notify_extra_frame(n_frames);
}

std::unique_ptr<lsfg_vk> lsfg_ptr;
