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

/***************************************************************************************************
 *
 * Per-Device concrete/derived class for the "Android fallback" path (uses
 * Choreographer to try to get presents to occur at the desired time).
 *
 ***************************************************************************************************/

#pragma once

#include "SwappyVkBase.h"

namespace swappy {

class SwappyVkFallback : public SwappyVkBase {
   public:
    SwappyVkFallback(//JNIEnv* env, jobject jactivity,
                     VkPhysicalDevice physicalDevice, VkDevice device,
                     const SwappyVkFunctionProvider* provider);

    virtual bool doGetRefreshCycleDuration(VkSwapchainKHR swapchain,
                                           uint64_t* pRefreshDuration) override;

    virtual VkResult doQueuePresent(
        VkQueue queue, uint32_t queueFamilyIndex,
        const VkPresentInfoKHR* pPresentInfo) override;
};

}  // namespace swappy
