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

// API entry points

#include "SwappyVk.h"
#include "swappy/swappyVk.h"

extern "C" {

// Internal function to track Swappy version bundled in a binary.
void SWAPPY_VERSION_SYMBOL();

void SwappyVk_determineDeviceExtensions(
    VkPhysicalDevice physicalDevice, uint32_t availableExtensionCount,
    VkExtensionProperties* pAvailableExtensions,
    uint32_t* pRequiredExtensionCount, char** pRequiredExtensions) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.swappyVkDetermineDeviceExtensions(
        physicalDevice, availableExtensionCount, pAvailableExtensions,
        pRequiredExtensionCount, pRequiredExtensions);
}

void SwappyVk_setQueueFamilyIndex(VkDevice device, VkQueue queue,
                                  uint32_t queueFamilyIndex) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetQueueFamilyIndex(device, queue, queueFamilyIndex);
}

bool SwappyVk_initAndGetRefreshCycleDuration(JNIEnv* env, jobject jactivity,
                                             VkPhysicalDevice physicalDevice,
                                             VkDevice device,
                                             VkSwapchainKHR swapchain,
                                             uint64_t* pRefreshDuration) {
    SWAPPY_VERSION_SYMBOL();
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    return swappy.GetRefreshCycleDuration(env, jactivity, physicalDevice,
                                          device, swapchain, pRefreshDuration);
}

void SwappyVk_setWindow(VkDevice device, VkSwapchainKHR swapchain,
                        ANativeWindow* window) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetWindow(device, swapchain, window);
}

void SwappyVk_setSwapIntervalNS(VkDevice device, VkSwapchainKHR swapchain,
                                uint64_t swap_ns) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetSwapDuration(device, swapchain, swap_ns);
}

VkResult SwappyVk_queuePresent(VkQueue queue,
                               const VkPresentInfoKHR* pPresentInfo) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    return swappy.QueuePresent(queue, pPresentInfo);
}

void SwappyVk_destroySwapchain(VkDevice device, VkSwapchainKHR swapchain) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.DestroySwapchain(device, swapchain);
}

void SwappyVk_destroyDevice(VkDevice device) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.DestroyDevice(device);
}

void SwappyVk_setAutoSwapInterval(bool enabled) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetAutoSwapInterval(enabled);
}

void SwappyVk_setAutoPipelineMode(bool enabled) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetAutoPipelineMode(enabled);
}

void SwappyVk_setFenceTimeoutNS(uint64_t fence_timeout_ns) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetFenceTimeout(std::chrono::nanoseconds(fence_timeout_ns));
}

void SwappyVk_setMaxAutoSwapIntervalNS(uint64_t max_swap_ns) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetMaxAutoSwapDuration(std::chrono::nanoseconds(max_swap_ns));
}

uint64_t SwappyVk_getFenceTimeoutNS() {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    return swappy.GetFenceTimeout().count();
}

void SwappyVk_injectTracer(const SwappyTracer* t) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.addTracer(t);
}

void SwappyVk_setFunctionProvider(
    const SwappyVkFunctionProvider* pSwappyVkFunctionProvider) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetFunctionProvider(pSwappyVkFunctionProvider);
}

uint64_t SwappyVk_getSwapIntervalNS(VkSwapchainKHR swapchain) {
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    return swappy.GetSwapInterval(swapchain).count();
}

}  // extern "C"
