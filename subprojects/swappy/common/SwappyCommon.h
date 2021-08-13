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

//#include <jni.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "CPUTracer.h"
#include "Settings.h"
#include "ChoreographerFilter.h"
#include "ChoreographerThread.h"
#include "FrameStatistics.h"
#include "SwappyDisplayManager.h"
#include "Thread.h"
//#include "swappy/swappyGL.h"
//#include "swappy/swappyGL_extra.h"

namespace swappy {

// ANativeWindow_setFrameRate is supported from API 30. To allow compilation for
// minSDK < 30 we need runtime support to call this API.
using PFN_ANativeWindow_setFrameRate = int32_t (*)(ANativeWindow* window,
                                                   float frameRate,
                                                   int8_t compatibility);

using namespace std::chrono_literals;

struct SwappyCommonSettings {
    SdkVersion sdkVersion;

    std::chrono::nanoseconds refreshPeriod;
    std::chrono::nanoseconds appVsyncOffset;
    std::chrono::nanoseconds sfVsyncOffset;

    static bool getFromApp(//JNIEnv* env, jobject jactivity,
                           SwappyCommonSettings* out);
    static SdkVersion getSDKVersion(/*JNIEnv* env*/);
    static bool queryDisplayTimings(//JNIEnv* env, jobject jactivity,
                                    SwappyCommonSettings* out);
};

// Common part between OpenGL and Vulkan implementations.
class SwappyCommon {
   public:
    enum class PipelineMode { Off, On };

    // callbacks to be called during pre/post swap
    struct SwapHandlers {
        std::function<bool()> lastFrameIsComplete;
        std::function<std::chrono::nanoseconds()> getPrevFrameGpuTime;
    };

    SwappyCommon(/*JNIEnv* env, jobject jactivity*/);

    ~SwappyCommon();

    std::chrono::nanoseconds getSwapDuration();

    void onChoreographer(int64_t frameTimeNanos);

    void onPreSwap(const SwapHandlers& h);

    bool needToSetPresentationTime() { return mPresentationTimeNeeded; }

    void onPostSwap(const SwapHandlers& h);

    PipelineMode getCurrentPipelineMode() { return mPipelineMode; }

    template <typename... T>
    using Tracer = std::function<void(T...)>;
    void addTracerCallbacks(SwappyTracer tracer);

    void setAutoSwapInterval(bool enabled);
    void setAutoPipelineMode(bool enabled);

    void setMaxAutoSwapDuration(std::chrono::nanoseconds swapDuration) {
        mAutoSwapIntervalThreshold = swapDuration;
    }

    std::chrono::steady_clock::time_point getPresentationTime() {
        return mPresentationTime;
    }
    std::chrono::nanoseconds getRefreshPeriod() const {
        return mCommonSettings.refreshPeriod;
    }

    bool isValid() { return mValid; }

    std::chrono::nanoseconds getFenceTimeout() const { return mFenceTimeout; }
    void setFenceTimeout(std::chrono::nanoseconds t) { mFenceTimeout = t; }

    bool isDeviceUnsupported();

    void setANativeWindow(ANativeWindow* window);

    void setFrameStatistics(
        const std::shared_ptr<FrameStatistics>& frameStats) {
        mFrameStatistics = frameStats;
    }

    void setBufferStuffingFixWait(int32_t nFrames) {
        mBufferStuffingFixWait = std::max(0, nFrames);
    }

   protected:
    // Used for testing
    SwappyCommon(const SwappyCommonSettings& settings);

   private:
    class FrameDuration {
       public:
        FrameDuration() = default;

        FrameDuration(std::chrono::nanoseconds cpuTime,
                      std::chrono::nanoseconds gpuTime,
                      bool frameMissedDeadline)
            : mCpuTime(cpuTime),
              mGpuTime(gpuTime),
              mFrameMissedDeadline(frameMissedDeadline) {
            mCpuTime = std::min(mCpuTime, MAX_DURATION);
            mGpuTime = std::min(mGpuTime, MAX_DURATION);
        }

        std::chrono::nanoseconds getCpuTime() const { return mCpuTime; }
        std::chrono::nanoseconds getGpuTime() const { return mGpuTime; }

        bool frameMiss() const { return mFrameMissedDeadline; }

        std::chrono::nanoseconds getTime(PipelineMode pipeline) const {
            if (mCpuTime == 0ns && mGpuTime == 0ns) {
                return 0ns;
            }

            if (pipeline == PipelineMode::On) {
                return std::max(mCpuTime, mGpuTime) + FRAME_MARGIN;
            }

            return mCpuTime + mGpuTime + FRAME_MARGIN;
        }

        FrameDuration& operator+=(const FrameDuration& other) {
            mCpuTime += other.mCpuTime;
            mGpuTime += other.mGpuTime;
            return *this;
        }

        FrameDuration& operator-=(const FrameDuration& other) {
            mCpuTime -= other.mCpuTime;
            mGpuTime -= other.mGpuTime;
            return *this;
        }

        friend FrameDuration operator/(FrameDuration lhs, int rhs) {
            lhs.mCpuTime /= rhs;
            lhs.mGpuTime /= rhs;
            return lhs;
        }

       private:
        std::chrono::nanoseconds mCpuTime = std::chrono::nanoseconds(0);
        std::chrono::nanoseconds mGpuTime = std::chrono::nanoseconds(0);
        bool mFrameMissedDeadline = false;

        static constexpr std::chrono::nanoseconds MAX_DURATION =
            std::chrono::milliseconds(100);
    };

    void addFrameDuration(FrameDuration duration);
    std::chrono::nanoseconds wakeClient();

    bool swapFaster(int newSwapInterval) REQUIRES(mMutex);

    bool swapSlower(const FrameDuration& averageFrameTime,
                    const std::chrono::nanoseconds& upperBound,
                    int newSwapInterval) REQUIRES(mMutex);
    bool updateSwapInterval();
    void preSwapBuffersCallbacks();
    void postSwapBuffersCallbacks();
    void preWaitCallbacks();
    void postWaitCallbacks(std::chrono::nanoseconds cpuTime,
                           std::chrono::nanoseconds gpuTime);
    void startFrameCallbacks();
    void swapIntervalChangedCallbacks();
    void onSettingsChanged();
    void updateMeasuredSwapDuration(std::chrono::nanoseconds duration);
    void startFrame();
    void waitUntil(int32_t target);
    void waitUntilTargetFrame();
    void waitOneFrame();
    void setPreferredDisplayModeId(int index);
    void setPreferredRefreshPeriod(std::chrono::nanoseconds frameTime)
        REQUIRES(mMutex);
    int calculateSwapInterval(std::chrono::nanoseconds frameTime,
                              std::chrono::nanoseconds refreshPeriod);
    void updateDisplayTimings();

    // Waits for the next frame, considering both Choreographer and the prior
    // frame's completion
    bool waitForNextFrame(const SwapHandlers& h);

    void onRefreshRateChanged();

    inline bool swapFasterCondition() {
        return mSwapDuration <=
               mCommonSettings.refreshPeriod * (mAutoSwapInterval - 1) +
                   DURATION_ROUNDING_MARGIN;
    }

//     const jobject mJactivity;
//     void* mLibAndroid = nullptr;
    PFN_ANativeWindow_setFrameRate mANativeWindow_setFrameRate = nullptr;

//     JavaVM* mJVM = nullptr;

    SwappyCommonSettings mCommonSettings;

    std::unique_ptr<ChoreographerFilter> mChoreographerFilter;

    bool mUsingExternalChoreographer = false;
    std::unique_ptr<ChoreographerThread> mChoreographerThread;
//     std::thread mWakerThread;
//     bool mQuitThread = false;

    std::mutex mWaitingMutex;
    std::condition_variable mWaitingCondition;
    std::chrono::steady_clock::time_point mCurrentFrameTimestamp =
        std::chrono::steady_clock::now();
    int32_t mCurrentFrame = 0;
    std::atomic<std::chrono::nanoseconds> mMeasuredSwapDuration;

    std::chrono::steady_clock::time_point mSwapTime;

    std::mutex mMutex;
    class FrameDurations {
       public:
        void add(FrameDuration frameDuration);
        bool hasEnoughSamples() const;
        FrameDuration getAverageFrameTime() const;
        int getMissedFramePercent() const;
        void clear();

       private:
        static constexpr std::chrono::nanoseconds
            FRAME_DURATION_SAMPLE_SECONDS = 2s;

        std::deque<std::pair<std::chrono::time_point<std::chrono::steady_clock>,
                             FrameDuration>>
            mFrames;
        FrameDuration mFrameDurationsSum = {};
        int mMissedFrameCount = 0;
    };

    FrameDurations mFrameDurations GUARDED_BY(mMutex);

    bool mAutoSwapIntervalEnabled GUARDED_BY(mMutex) = true;
    bool mPipelineModeAutoMode GUARDED_BY(mMutex) = true;

    static constexpr std::chrono::nanoseconds FRAME_MARGIN = 1ms;
    static constexpr std::chrono::nanoseconds DURATION_ROUNDING_MARGIN = 1us;
    static constexpr int NON_PIPELINE_PERCENT = 50;  // 50%
    static constexpr int FRAME_DROP_THRESHOLD = 10;  // 10%

    std::chrono::nanoseconds mSwapDuration = 0ns;
    int32_t mAutoSwapInterval;
    std::atomic<std::chrono::nanoseconds> mAutoSwapIntervalThreshold = {
        50ms};  // 20FPS
    static constexpr std::chrono::nanoseconds REFRESH_RATE_MARGIN = 500ns;

    std::chrono::steady_clock::time_point mStartFrameTime;

    struct SwappyTracerCallbacks {
        std::list<Tracer<>> preWait;
        std::list<Tracer<int64_t, int64_t>> postWait;
        std::list<Tracer<>> preSwapBuffers;
        std::list<Tracer<int64_t>> postSwapBuffers;
        std::list<Tracer<int32_t, long>> startFrame;
        std::list<Tracer<>> swapIntervalChanged;
    };

    SwappyTracerCallbacks mInjectedTracers;

    int32_t mTargetFrame = 0;
    std::chrono::steady_clock::time_point mPresentationTime =
        std::chrono::steady_clock::now();
    bool mPresentationTimeNeeded;
    PipelineMode mPipelineMode = PipelineMode::On;

    bool mValid;

    std::chrono::nanoseconds mFenceTimeout = std::chrono::nanoseconds(50ms);

    constexpr static bool USE_DISPLAY_MANAGER = true;
    std::unique_ptr<SwappyDisplayManager> mDisplayManager;
    int mNextModeId = -1;

    std::shared_ptr<SwappyDisplayManager::RefreshPeriodMap>
        mSupportedRefreshPeriods;

    struct TimingSettings {
        std::chrono::nanoseconds refreshPeriod = {};
        std::chrono::nanoseconds swapDuration = {};

        static TimingSettings from(const Settings& settings) {
            TimingSettings timingSettings;

            timingSettings.refreshPeriod =
                settings.getDisplayTimings().refreshPeriod;
            timingSettings.swapDuration = settings.getSwapDuration();
            return timingSettings;
        }

        bool operator!=(const TimingSettings& other) const {
            return (refreshPeriod != other.refreshPeriod) ||
                   (swapDuration != other.swapDuration);
        }

        bool operator==(const TimingSettings& other) const {
            return !(*this != other);
        }
    };
    TimingSettings mNextTimingSettings GUARDED_BY(mMutex) = {};
    bool mTimingSettingsNeedUpdate GUARDED_BY(mMutex) = false;

//     CPUTracer mCPUTracer;

    ANativeWindow* mWindow GUARDED_BY(mMutex) = nullptr;
    bool mWindowChanged GUARDED_BY(mMutex) = false;
    float mLatestFrameRateVote GUARDED_BY(mMutex) = 0.f;
    static constexpr float FRAME_RATE_VOTE_MARGIN = 1.f;  // 1Hz

    // If zero, don't apply the double buffering fix. If non-zero, apply
    // the fix after this number of bad frames.
    int mBufferStuffingFixWait = 0;
    // When zero, buffer stuffing fixing may occur.
    // After a fix has been applied, this is non-zero and counts down to avoid
    // consecutive fixes.
    int mBufferStuffingFixCounter = 0;
    // Counts the number of consecutive missed frames (as judged by expected
    // latency).
    int mMissedFrameCounter = 0;

    std::shared_ptr<FrameStatistics> mFrameStatistics;
};

}  // namespace swappy
