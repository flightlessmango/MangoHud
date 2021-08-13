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

#if (not defined ANDROID_NDK_VERSION) || ANDROID_NDK_VERSION >= 15

#include "SwappyVkGoogleDisplayTiming.h"

#define LOG_TAG "SwappyVkGoogleDisplayTiming"

using std::chrono::nanoseconds;

namespace swappy {

SwappyVkGoogleDisplayTiming::SwappyVkGoogleDisplayTiming(
    /*JNIEnv* env, jobject jactivity,*/ VkPhysicalDevice physicalDevice,
    VkDevice device, const SwappyVkFunctionProvider* provider)
    : SwappyVkBase(/*env, jactivity,*/ physicalDevice, device, provider) {}

bool SwappyVkGoogleDisplayTiming::doGetRefreshCycleDuration(
    VkSwapchainKHR swapchain, uint64_t* pRefreshDuration) {
    if (!isEnabled()) {
        ALOGE("Swappy is disabled.");
        return false;
    }

    VkRefreshCycleDurationGOOGLE refreshCycleDuration;
    VkResult res = mpfnGetRefreshCycleDurationGOOGLE(mDevice, swapchain,
                                                     &refreshCycleDuration);
    if (res != VK_SUCCESS) {
        ALOGE("mpfnGetRefreshCycleDurationGOOGLE failed %d", res);
        return false;
    }

    *pRefreshDuration = mCommonBase.getRefreshPeriod().count();

    double refreshRate = 1000000000.0 / *pRefreshDuration;
    ALOGI("Returning refresh duration of %" PRIu64 " nsec (approx %f Hz)",
          *pRefreshDuration, refreshRate);

    return true;
}

VkResult SwappyVkGoogleDisplayTiming::doQueuePresent(
    VkQueue queue, uint32_t queueFamilyIndex,
    const VkPresentInfoKHR* pPresentInfo) {
    if (!isEnabled()) {
        ALOGE("Swappy is disabled.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult res = initializeVkSyncObjects(queue, queueFamilyIndex);
    if (res) {
        return res;
    }

    const SwappyCommon::SwapHandlers handlers = {
        .lastFrameIsComplete = std::bind(
            &SwappyVkGoogleDisplayTiming::lastFrameIsCompleted, this, queue),
        .getPrevFrameGpuTime = std::bind(
            &SwappyVkGoogleDisplayTiming::getLastFenceTime, this, queue),
    };

    VkSemaphore semaphore;
    res = injectFence(queue, pPresentInfo, &semaphore);
    if (res) {
        ALOGE("Failed to vkQueueSubmit %d", res);
        return res;
    }

    uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    if (semaphore != VK_NULL_HANDLE) {
        waitSemaphoreCount = 1;
        pWaitSemaphores = &semaphore;
    } else {
        waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
        pWaitSemaphores = pPresentInfo->pWaitSemaphores;
    }

    mCommonBase.onPreSwap(handlers);

    VkPresentTimeGOOGLE pPresentTimes[pPresentInfo->swapchainCount];
    VkPresentInfoKHR replacementPresentInfo;
    VkPresentTimesInfoGOOGLE presentTimesInfo;
    if (mCommonBase.needToSetPresentationTime()) {
        // Setup the new structures to pass:
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
            pPresentTimes[i].presentID = mNextPresentID;
            pPresentTimes[i].desiredPresentTime =
                mCommonBase.getPresentationTime().time_since_epoch().count();
        }

        presentTimesInfo = {VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
                            pPresentInfo->pNext, pPresentInfo->swapchainCount,
                            pPresentTimes};

        replacementPresentInfo = {
            pPresentInfo->sType,          &presentTimesInfo,
            waitSemaphoreCount,           pWaitSemaphores,
            pPresentInfo->swapchainCount, pPresentInfo->pSwapchains,
            pPresentInfo->pImageIndices,  pPresentInfo->pResults};

    } else {
        replacementPresentInfo = {
            pPresentInfo->sType,          nullptr,
            waitSemaphoreCount,           pWaitSemaphores,
            pPresentInfo->swapchainCount, pPresentInfo->pSwapchains,
            pPresentInfo->pImageIndices,  pPresentInfo->pResults};
    }
    mNextPresentID++;

    res = mpfnQueuePresentKHR(queue, &replacementPresentInfo);
    mCommonBase.onPostSwap(handlers);

    return res;
}

}  // namespace swappy

#endif  // #if (not defined ANDROID_NDK_VERSION) || ANDROID_NDK_VERSION>=15
