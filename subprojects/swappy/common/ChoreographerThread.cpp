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

#define LOG_TAG "ChoreographerThread"

#include "ChoreographerThread.h"

// #include <android/looper.h>
// #include <jni.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "ChoreographerShim.h"
#include "CpuInfo.h"
// #include "JNIUtil.h"
#include "Log.h"
#include "Settings.h"
#include "Thread.h"
// #include "Trace.h"

namespace swappy {

// AChoreographer is supported from API 24. To allow compilation for minSDK < 24
// and still use AChoreographer for SDK >= 24 we need runtime support to call
// AChoreographer APIs.

using PFN_AChoreographer_getInstance = AChoreographer *(*)();

using PFN_AChoreographer_postFrameCallback =
    void (*)(AChoreographer *choreographer,
             AChoreographer_frameCallback callback, void *data);

using PFN_AChoreographer_postFrameCallbackDelayed = void (*)(
    AChoreographer *choreographer, AChoreographer_frameCallback callback,
    void *data, long delayMillis);

using PFN_AChoreographer_registerRefreshRateCallback =
    void (*)(AChoreographer *choreographer,
             AChoreographer_refreshRateCallback callback, void *data);

using PFN_AChoreographer_unregisterRefreshRateCallback =
    void (*)(AChoreographer *choreographer,
             AChoreographer_refreshRateCallback callback, void *data);


class NoChoreographerThread : public ChoreographerThread {
   public:
    NoChoreographerThread(Callback onChoreographer);
    ~NoChoreographerThread();

   private:
    void postFrameCallbacks() override;
    void scheduleNextFrameCallback() override REQUIRES(mWaitingMutex);
    void looperThread();
    void onSettingsChanged();

    Thread mThread;
    bool mThreadRunning GUARDED_BY(mWaitingMutex);
    std::condition_variable_any mWaitingCondition GUARDED_BY(mWaitingMutex);
    std::chrono::nanoseconds mRefreshPeriod GUARDED_BY(mWaitingMutex);
};

NoChoreographerThread::NoChoreographerThread(Callback onChoreographer)
    : ChoreographerThread(onChoreographer) {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });
    mThreadRunning = true;
    mThread = Thread([this]() { looperThread(); });
    mInitialized = true;
}

NoChoreographerThread::~NoChoreographerThread() {
    ALOGI("Destroying NoChoreographerThread");
    {
        std::lock_guard<std::mutex> lock(mWaitingMutex);
        mThreadRunning = false;
    }
    mWaitingCondition.notify_all();
    mThread.join();
}

void NoChoreographerThread::onSettingsChanged() {
    const Settings::DisplayTimings &displayTimings =
        Settings::getInstance()->getDisplayTimings();
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    mRefreshPeriod = displayTimings.refreshPeriod;
    ALOGV("onSettingsChanged(): refreshPeriod={}",
          (long long)displayTimings.refreshPeriod.count());
}

void NoChoreographerThread::looperThread() {
    const char *name = "SwappyChoreographer";

    CpuInfo cpu;
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(0, &cpu_set);

    if (cpu.getNumberOfCpus() > 0) {
        ALOGI("Swappy found {} CPUs [{}].", cpu.getNumberOfCpus(),
              cpu.getHardware().c_str());
        if (cpu.getNumberOfLittleCores() > 0) {
            cpu_set = cpu.getLittleCoresMask();
        }
    }

    const auto tid = gettid();
    ALOGI("Setting '{}' thread [{}-0x{:x}] affinity mask to 0x{:x}.", name, tid,
          tid, to_mask(cpu_set));
    sched_setaffinity(tid, sizeof(cpu_set), &cpu_set);

    pthread_setname_np(pthread_self(), name);

    auto wakeTime = std::chrono::steady_clock::now();

    while (true) {
        {
            // mutex should be unlocked before sleeping
            std::lock_guard<std::mutex> lock(mWaitingMutex);
            if (!mThreadRunning) {
                break;
            }
            mWaitingCondition.wait(mWaitingMutex);
            if (!mThreadRunning) {
                break;
            }

            const auto timePassed = std::chrono::steady_clock::now() - wakeTime;
            const int intervals = std::floor(timePassed / mRefreshPeriod);
            wakeTime += (intervals + 1) * mRefreshPeriod;
        }

        SPDLOG_DEBUG("sleeping {}", wakeTime.count());
        std::this_thread::sleep_until(wakeTime);
        mCallback();
    }
    ALOGI("Terminating choreographer thread");
}

void NoChoreographerThread::postFrameCallbacks() {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    mWaitingCondition.notify_one();
}

void NoChoreographerThread::scheduleNextFrameCallback() {}

ChoreographerThread::ChoreographerThread(Callback onChoreographer)
    : mCallback(onChoreographer) {}

ChoreographerThread::~ChoreographerThread() = default;

void ChoreographerThread::postFrameCallbacks() {
//     TRACE_CALL();

    // This method is called before calling to swap buffers
    // It registers to get MAX_CALLBACKS_BEFORE_IDLE frame callbacks before
    // going idle so if app goes to idle the thread will not get further frame
    // callbacks
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    if (mCallbacksBeforeIdle == 0) {
        scheduleNextFrameCallback();
    }
    mCallbacksBeforeIdle = MAX_CALLBACKS_BEFORE_IDLE;
}

void ChoreographerThread::onChoreographer() {
//     TRACE_CALL();

    {
        std::lock_guard<std::mutex> lock(mWaitingMutex);
        mCallbacksBeforeIdle--;

        if (mCallbacksBeforeIdle > 0) {
            scheduleNextFrameCallback();
        }
    }
    mCallback();
}

std::unique_ptr<ChoreographerThread>
ChoreographerThread::createChoreographerThread(Type type,
                                               Callback onChoreographer,
                                               Callback onRefreshRateChanged,
                                               SdkVersion sdkVersion) {
//     if (type == Type::App) {
//         ALOGI("Using Application's Choreographer");
//         return std::make_unique<NoChoreographerThread>(onChoreographer);
//     }

    ALOGI("Using no Choreographer (Best Effort)");
    return std::make_unique<NoChoreographerThread>(onChoreographer);
}

}  // namespace swappy
