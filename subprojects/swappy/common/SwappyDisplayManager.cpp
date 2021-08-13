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

#define LOG_TAG "SwappyDisplayManager"

#include "SwappyDisplayManager.h"

#include "Log.h"
//#include <android/looper.h>
//#include <jni.h>

#include <map>

//#include "JNIUtil.h"
#include "Settings.h"

namespace swappy {

bool SwappyDisplayManager::useSwappyDisplayManager(SdkVersion sdkVersion) {
    return false;
}

SwappyDisplayManager::SwappyDisplayManager()
{
}

SwappyDisplayManager::~SwappyDisplayManager() {
}

std::shared_ptr<SwappyDisplayManager::RefreshPeriodMap>
SwappyDisplayManager::getSupportedRefreshPeriods() {
    std::unique_lock<std::mutex> lock(mMutex);

    mCondition.wait(
        lock, [&]() { return mSupportedRefreshPeriods.get() != nullptr; });
    return mSupportedRefreshPeriods;
}

void SwappyDisplayManager::setPreferredDisplayModeId(int index) {
}

// // Helper class to wrap JNI entry points to SwappyDisplayManager
// class SwappyDisplayManagerJNI {
//    public:
//     static void onSetSupportedRefreshPeriods(
//         long, std::shared_ptr<SwappyDisplayManager::RefreshPeriodMap>);
//     static void onRefreshPeriodChanged(long, long, long, long);
// };
//
// void SwappyDisplayManagerJNI::onSetSupportedRefreshPeriods(
//     std::shared_ptr<SwappyDisplayManager::RefreshPeriodMap> refreshPeriods) {
//     auto *sDM = reinterpret_cast<SwappyDisplayManager *>(cookie);
//
//     std::lock_guard<std::mutex> lock(sDM->mMutex);
//     sDM->mSupportedRefreshPeriods = std::move(refreshPeriods);
//     sDM->mCondition.notify_one();
// }
//
// void SwappyDisplayManagerJNI::onRefreshPeriodChanged(//jlong /*cookie*/,
//                                                      long refreshPeriod,
//                                                      long appOffset,
//                                                      long sfOffset) {
//     ALOGV("onRefreshPeriodChanged: refresh rate: %.0fHz", 1e9f / refreshPeriod);
//     using std::chrono::nanoseconds;
//     Settings::DisplayTimings displayTimings;
//     displayTimings.refreshPeriod = nanoseconds(refreshPeriod);
//     displayTimings.appOffset = nanoseconds(appOffset);
//     displayTimings.sfOffset = nanoseconds(sfOffset);
//     Settings::getInstance()->setDisplayTimings(displayTimings);
// }

}  // namespace swappy
