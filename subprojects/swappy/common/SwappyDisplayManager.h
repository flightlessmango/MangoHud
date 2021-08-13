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

//#include <jni.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace swappy {

struct SdkVersion {
    int sdkInt;         // Build.VERSION.SDK_INT
    int previewSdkInt;  // Build.VERSION.PREVIEW_SDK_INT
};

class SwappyDisplayManager {
   public:
    static bool useSwappyDisplayManager(SdkVersion sdkVersion);

    SwappyDisplayManager(/*...*/);
    ~SwappyDisplayManager();

    bool isInitialized() { return mInitialized; }

    // Map from refresh period to display mode id
    using RefreshPeriodMap = std::map<std::chrono::nanoseconds, int>;

    std::shared_ptr<RefreshPeriodMap> getSupportedRefreshPeriods();

    void setPreferredDisplayModeId(int index);

   private:
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::shared_ptr<RefreshPeriodMap> mSupportedRefreshPeriods;
    //jmethodID mSetPreferredDisplayModeId = nullptr;
    //jmethodID mTerminate = nullptr;
    bool mInitialized = false;

};

}  // namespace swappy
