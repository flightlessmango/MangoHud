#include "lsfg-vk.h"
#include "fps_metrics.h"
#include "hud_elements.h"
#include "overlay.h"
#include "mesa/util/macros.h"
#include <link.h>
#include "fps_metrics.h"

void lsfg_vk::notify_extra_frame(uint64_t n_frames, VkSwapchainKHR swapchain, bool generated) {
    swapchain_stats* sw_stats = get_swapchain_stats_from_swapchain(swapchain);
    if (!sw_stats)
        return;

    sw_stats->engine = EngineTypes::LSFG;

    uint64_t now = os_time_get_nano();

    static uint64_t last_real_present = 0;
    static uint64_t last_fake_present = 0;

    uint64_t frametime_ns = 0;

    if (generated) {
        if (last_fake_present == 0)
            last_fake_present = now;

        frametime_ns = now - last_fake_present;
        last_fake_present = now;

        float frametime_ms = frametime_ns / 1000000.f;
        uint32_t idx = n_frames_fake % ARRAY_SIZE(fake_frames);
        fake_frames[idx] = frametime_ms;
        n_frames_fake++;
        n_frames_since_update_fake++;
    } else {
        if (last_real_present == 0)
            last_real_present = now;

        frametime_ns = now - last_real_present;
        last_real_present = now;

        float frametime_ms = frametime_ns / 1000000.f;
        uint32_t idx = n_frames_real % ARRAY_SIZE(frametimes);
        frametimes[idx] = frametime_ms;
        n_frames_real++;
        n_frames_since_update_real++;
    }

    if (!get_params()->no_display || logger->is_active())
        update_hud_info_with_frametime(*sw_stats, *get_params(), HUDElements.vendorID, frametime_ns);
}

void lsfg_vk::update_display_fps(uint64_t now) {
    static uint64_t last_fps_update = 0;
    auto elapsed = now - last_fps_update;
    if (elapsed == 0)
        return;

    fake_fps = 1000000000.0 * n_frames_since_update_fake / elapsed;
    real_fps = 1000000000.0 * n_frames_since_update_real / elapsed;
    n_frames_since_update_fake = 0;
    n_frames_since_update_real = 0;
    last_fps_update = now;
}

extern "C" __attribute__((visibility("default"))) __attribute__((used))
void notify_extra_frame(uint64_t n_frames, VkSwapchainKHR swapchain, bool fake) {
    if (!lsfg_ptr)
        lsfg_ptr = std::make_unique<lsfg_vk>();

    lsfg_ptr->notify_extra_frame(n_frames, swapchain, fake);
}

std::unique_ptr<lsfg_vk> lsfg_ptr;
