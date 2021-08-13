/*
 * Copyright 2018 The Android Open Source Project
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

#include <condition_variable>
#include <mutex>
#include <vector>

#include "Settings.h"
#include "Thread.h"

namespace swappy {

class ChoreographerFilter {
   public:
    using Worker = std::function<std::chrono::nanoseconds()>;

    explicit ChoreographerFilter(std::chrono::nanoseconds refreshPeriod,
                                 std::chrono::nanoseconds appToSfDelay,
                                 Worker doWork);
    ~ChoreographerFilter();

    void onChoreographer();

   private:
    void launchThreadsLocked();
    void terminateThreadsLocked();

    void onSettingsChanged();

    void threadMain(bool useAffinity, int32_t thread);

    std::mutex mThreadPoolMutex;
    bool mUseAffinity = true;
    std::vector<Thread> mThreadPool;

    std::mutex mMutex;
    std::condition_variable mCondition;
    bool mIsRunning = true;
    int64_t mSequenceNumber = 0;
    std::chrono::steady_clock::time_point mLastTimestamp;

    std::mutex mWorkMutex;
    std::chrono::steady_clock::time_point mLastWorkRun;
    std::chrono::nanoseconds mWorkDuration;

    std::chrono::nanoseconds mRefreshPeriod;
    std::chrono::nanoseconds mAppToSfDelay;
    const Worker mDoWork;
};

}  // namespace swappy
