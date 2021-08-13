/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "SwappyVkBase.h"

namespace swappy {

/***************************************************************************************************
 *
 * Per-Device concrete/derived class for using VK_GOOGLE_display_timing.
 *
 * This class uses the VK_GOOGLE_display_timing in order to present frames at a
 *muliple (the "swap interval") of a fixed refresh-cycle duration (i.e. the time
 *between successive vsync's).
 *
 * In order to reduce complexity, some simplifying assumptions are made:
 *
 * - We assume a fixed refresh-rate (FRR) display that's between 60 Hz and 120
 *Hz.
 *
 * - While Vulkan allows applications to create and use multiple
 *VkSwapchainKHR's per VkDevice, and to re-create VkSwapchainKHR's, we assume
 *that the application uses a single VkSwapchainKHR, and never re-creates it.
 *
 * - The values reported back by the VK_GOOGLE_display_timing extension (which
 *comes from lower-level Android interfaces) are not precise, and that values
 *can drift over time.  For example, the refresh-cycle duration for a 60 Hz
 *display should be 16,666,666 nsec; but the value reported back by the
 *extension won't be precisely this.  Also, the differences betweeen the times
 *of two successive frames won't be an exact multiple of 16,666,666 nsec.  This
 *can make it difficult to precisely predict when a future vsync will be (it can
 *appear to drift overtime).  Therefore, we try to give a desiredPresentTime for
 *each image that is between 3 and 7 msec before vsync.  We look at the
 *actualPresentTime for previously-presented images, and nudge the future
 *desiredPresentTime back within those 3-7 msec boundaries.
 *
 * - There can be a few frames of latency between when an image is presented and
 *when the actualPresentTime is available for that image.  Therefore, we
 *initially just pick times based upon CLOCK_MONOTONIC (which is the time domain
 *for VK_GOOGLE_display_timing).  After we get past-present times, we nudge the
 *desiredPresentTime, we wait for a few presents before looking again to see
 *whether we need to nudge again.
 *
 * - If, for some reason, an application can't keep up with its chosen swap
 *interval (e.g. it's designed for 30FPS on a premium device and is now running
 *on a slow device; or it's running on a 120Hz display), this algorithm may not
 *be able to make up for this (i.e. smooth rendering at a targeted frame rate
 *may not be possible with an application that can't render fast enough).
 *
 ***************************************************************************************************/
class SwappyVkGoogleDisplayTiming : public SwappyVkBase {
   public:
    SwappyVkGoogleDisplayTiming(//JNIEnv* env, jobject jactivity,
                                VkPhysicalDevice physicalDevice,
                                VkDevice device,
                                const SwappyVkFunctionProvider* provider);

    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                           uint64_t* pRefreshDuration) override;

    virtual VkResult doQueuePresent(
        VkQueue queue, uint32_t queueFamilyIndex,
        const VkPresentInfoKHR* pPresentInfo) override;
};

}  // namespace swappy
