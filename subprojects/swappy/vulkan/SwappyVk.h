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
#include "SwappyVkFallback.h"
#include "SwappyVkGoogleDisplayTiming.h"

namespace swappy {

// The API functions call methods of the singleton SwappyVk class.
// Those methods call virtual methods of the abstract SwappyVkBase class,
// which is actually implemented by one of the derived/concrete classes:
//
// - SwappyVkGoogleDisplayTiming
// - SwappyVkFallback

/***************************************************************************************************
 *
 * Singleton class that provides the high-level implementation of the Swappy
 *entrypoints.
 *
 ***************************************************************************************************/
/**
 * Singleton class that provides the high-level implementation of the Swappy
 * entrypoints.
 *
 * This class determines which low-level implementation to use for each physical
 * device, and then calls that class's do-method for the entrypoint.
 */
class SwappyVk {
   public:
    static SwappyVk& getInstance() {
        static SwappyVk instance;
        return instance;
    }

    ~SwappyVk() {
        if (pFunctionProvider) {
            pFunctionProvider->close();
        }
    }

    void swappyVkDetermineDeviceExtensions(
        VkPhysicalDevice physicalDevice, uint32_t availableExtensionCount,
        VkExtensionProperties* pAvailableExtensions,
        uint32_t* pRequiredExtensionCount, char** pRequiredExtensions);
    void SetQueueFamilyIndex(VkDevice device, VkQueue queue,
                             uint32_t queueFamilyIndex);
    bool GetRefreshCycleDuration(//JNIEnv* env, jobject jactivity,
                                 VkPhysicalDevice physicalDevice,
                                 VkDevice device, VkSwapchainKHR swapchain,
                                 uint64_t* pRefreshDuration);
    void SetWindow(VkDevice device, VkSwapchainKHR swapchain,
                   ANativeWindow* window);
    void SetSwapDuration(VkDevice device, VkSwapchainKHR swapchain,
                         uint64_t swapNs);
    VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
    void DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain);
    void DestroyDevice(VkDevice device);

    void SetAutoSwapInterval(bool enabled);
    void SetAutoPipelineMode(bool enabled);
    void SetMaxAutoSwapDuration(std::chrono::nanoseconds maxDuration);
    void SetFenceTimeout(std::chrono::nanoseconds duration);
    std::chrono::nanoseconds GetFenceTimeout() const;
    std::chrono::nanoseconds GetSwapInterval(VkSwapchainKHR swapchain);

    void addTracer(const SwappyTracer* t);

    void SetFunctionProvider(const SwappyVkFunctionProvider* pFunctionProvider);
    bool InitFunctions();

   private:
    std::map<VkPhysicalDevice, bool> doesPhysicalDeviceHaveGoogleDisplayTiming;
    std::map<VkSwapchainKHR, std::shared_ptr<SwappyVkBase>>
        perSwapchainImplementation;

    struct QueueFamilyIndex {
        VkDevice device;
        uint32_t queueFamilyIndex;
    };
    std::map<VkQueue, QueueFamilyIndex> perQueueFamilyIndex;

    const SwappyVkFunctionProvider* pFunctionProvider = nullptr;

   private:
    SwappyVk() {}  // Need to implement this constructor

    // Forbid copies.
    SwappyVk(SwappyVk const&) = delete;
    void operator=(SwappyVk const&) = delete;
};

}  // namespace swappy
