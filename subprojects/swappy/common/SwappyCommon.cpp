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

#include "SwappyCommon.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>

 #include "Log.h"
#include "Settings.h"
#include "Thread.h"
// #include "Trace.h"

#define LOG_TAG "SwappyCommon"

namespace swappy {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

// NB These are only needed for C++14
constexpr nanoseconds SwappyCommon::FrameDuration::MAX_DURATION;
constexpr nanoseconds SwappyCommon::FRAME_MARGIN;
constexpr nanoseconds SwappyCommon::DURATION_ROUNDING_MARGIN;
constexpr nanoseconds SwappyCommon::REFRESH_RATE_MARGIN;
constexpr int SwappyCommon::NON_PIPELINE_PERCENT;
constexpr int SwappyCommon::FRAME_DROP_THRESHOLD;
constexpr std::chrono::nanoseconds
    SwappyCommon::FrameDurations::FRAME_DURATION_SAMPLE_SECONDS;

#if __ANDROID_API__ < 30
// Define ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_* to allow compilation on older
// versions
enum {
    /**
     * There are no inherent restrictions on the frame rate of this window.
     */
    ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT = 0,
    /**
     * This window is being used to display content with an inherently fixed
     * frame rate, e.g. a video that has a specific frame rate. When the system
     * selects a frame rate other than what the app requested, the app will need
     * to do pull down or use some other technique to adapt to the system's
     * frame rate. The user experience is likely to be worse (e.g. more frame
     * stuttering) than it would be if the system had chosen the app's requested
     * frame rate.
     */
    ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE = 1
};
#endif

bool SwappyCommonSettings::getFromApp(//JNIEnv* env, jobject jactivity,
                                      SwappyCommonSettings* out) {
    if (out == nullptr) return false;

    ALOGI("Swappy version {}.{}", SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION);

//     out->sdkVersion = getSDKVersion(env);
/*
    jclass activityClass = env->FindClass("android/app/NativeActivity");
    jclass windowManagerClass = env->FindClass("android/view/WindowManager");
    jclass displayClass = env->FindClass("android/view/Display");

    jmethodID getWindowManager = env->GetMethodID(
        activityClass, "getWindowManager", "()Landroid/view/WindowManager;");

    jmethodID getDefaultDisplay = env->GetMethodID(
        windowManagerClass, "getDefaultDisplay", "()Landroid/view/Display;");

    jobject wm = env->CallObjectMethod(jactivity, getWindowManager);
    jobject display = env->CallObjectMethod(wm, getDefaultDisplay);

    jmethodID getRefreshRate =
        env->GetMethodID(displayClass, "getRefreshRate", "()F");

    const float refreshRateHz = env->CallFloatMethod(display, getRefreshRate);

    jmethodID getAppVsyncOffsetNanos =
        env->GetMethodID(displayClass, "getAppVsyncOffsetNanos", "()J");

    // getAppVsyncOffsetNanos was only added in API 21.
    // Return gracefully if this device doesn't support it.
    if (getAppVsyncOffsetNanos == 0 || env->ExceptionOccurred()) {
        ALOGE("Error while getting method: getAppVsyncOffsetNanos");
        env->ExceptionClear();
        return false;
    }
    const long appVsyncOffsetNanos =
        env->CallLongMethod(display, getAppVsyncOffsetNanos);

    jmethodID getPresentationDeadlineNanos =
        env->GetMethodID(displayClass, "getPresentationDeadlineNanos", "()J");

    if (getPresentationDeadlineNanos == 0 || env->ExceptionOccurred()) {
        ALOGE("Error while getting method: getPresentationDeadlineNanos");
        return false;
    }

    const long vsyncPresentationDeadlineNanos =
        env->CallLongMethod(display, getPresentationDeadlineNanos);*/

    const long ONE_MS_IN_NS = 1000 * 1000;
    const long ONE_S_IN_NS = ONE_MS_IN_NS * 1000;

    // random hard coded crap
    auto appVsyncOffsetNanos = 0;
    const auto refreshRateHz = 144;
    auto vsyncPresentationDeadlineNanos = ONE_S_IN_NS / refreshRateHz;

    const long vsyncPeriodNanos =
        static_cast<long>(ONE_S_IN_NS / refreshRateHz);
    const long sfVsyncOffsetNanos =
        vsyncPeriodNanos - (vsyncPresentationDeadlineNanos - ONE_MS_IN_NS);

    using std::chrono::nanoseconds;
    out->refreshPeriod = nanoseconds(vsyncPeriodNanos);
    out->appVsyncOffset = nanoseconds(appVsyncOffsetNanos);
    out->sfVsyncOffset = nanoseconds(sfVsyncOffsetNanos);

    return true;
}

SwappyCommon::SwappyCommon(/*JNIEnv* env, jobject jactivity*/)
    : //mJactivity(env->NewGlobalRef(jactivity)),
      mMeasuredSwapDuration(nanoseconds(0)),
      mAutoSwapInterval(1),
      mValid(false) {
//     mLibAndroid = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
//     if (mLibAndroid == nullptr) {
//         ALOGE("FATAL: cannot open libandroid.so: %s", strerror(errno));
//         return;
//     }
//
//     mANativeWindow_setFrameRate =
//         reinterpret_cast<PFN_ANativeWindow_setFrameRate>(
//             dlsym(mLibAndroid, "ANativeWindow_setFrameRate"));
//
    if (!SwappyCommonSettings::getFromApp(/*env, mJactivity,*/ &mCommonSettings))
        return;
//
//     env->GetJavaVM(&mJVM);
//
//     if (isDeviceUnsupported()) {
//         ALOGE("Device is unsupported");
//         return;
//     }
//
    mChoreographerFilter = std::make_unique<ChoreographerFilter>(
        mCommonSettings.refreshPeriod,
        mCommonSettings.sfVsyncOffset - mCommonSettings.appVsyncOffset,
        [this]() { return wakeClient(); });

    mChoreographerThread = ChoreographerThread::createChoreographerThread(
        ChoreographerThread::Type::Swappy,
        [this] { mChoreographerFilter->onChoreographer(); },
        [this] { onRefreshRateChanged(); }, mCommonSettings.sdkVersion);
    if (!mChoreographerThread->isInitialized()) {
        ALOGE("failed to initialize ChoreographerThread");
        return;
    }

//     mWakerThread = std::thread ([this]{
//         while (!mQuitThread) {
//             wakeClient();
//             std::this_thread::sleep_for(7ms);
//         }
//     });

    if (USE_DISPLAY_MANAGER && SwappyDisplayManager::useSwappyDisplayManager(
                                   mCommonSettings.sdkVersion)) {
        mDisplayManager =
            std::make_unique<SwappyDisplayManager>(/*mJVM, jactivity*/);

        if (!mDisplayManager->isInitialized()) {
            mDisplayManager = nullptr;
            ALOGE("failed to initialize DisplayManager");
            return;
        }
    }

    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });
    Settings::getInstance()->setDisplayTimings({mCommonSettings.refreshPeriod,
                                                mCommonSettings.appVsyncOffset,
                                                mCommonSettings.sfVsyncOffset});

    ALOGI(
        "Initialized Swappy with vsyncPeriod={}, appOffset={}, "
        "sfOffset={}",
        (long long)mCommonSettings.refreshPeriod.count(),
        (long long)mCommonSettings.appVsyncOffset.count(),
        (long long)mCommonSettings.sfVsyncOffset.count());
    mValid = true;
}

// Used by tests
SwappyCommon::SwappyCommon(const SwappyCommonSettings& settings)
    : //mJactivity(nullptr),
      mCommonSettings(settings),
      mMeasuredSwapDuration(nanoseconds(0)),
      mAutoSwapInterval(1),
      mValid(true) {
    mChoreographerFilter = std::make_unique<ChoreographerFilter>(
        mCommonSettings.refreshPeriod,
        mCommonSettings.sfVsyncOffset - mCommonSettings.appVsyncOffset,
        [this]() { return wakeClient(); });
    mUsingExternalChoreographer = true;
    mChoreographerThread = ChoreographerThread::createChoreographerThread(
        ChoreographerThread::Type::App,
        [this] { mChoreographerFilter->onChoreographer(); }, [] {},
        mCommonSettings.sdkVersion);

    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });
    Settings::getInstance()->setDisplayTimings({mCommonSettings.refreshPeriod,
                                                mCommonSettings.appVsyncOffset,
                                                mCommonSettings.sfVsyncOffset});

    ALOGI(
        "Initialized Swappy with vsyncPeriod={}, appOffset={}, "
        "sfOffset={}",
        (long long)mCommonSettings.refreshPeriod.count(),
        (long long)mCommonSettings.appVsyncOffset.count(),
        (long long)mCommonSettings.sfVsyncOffset.count());
}

SwappyCommon::~SwappyCommon() {
    // destroy all threads first before the other members of this class
    mChoreographerThread.reset();
    mChoreographerFilter.reset();

    Settings::reset();
//     mQuitThread = true;
//     if (mWakerThread.joinable())
//         mWakerThread.join();

//     if (mJactivity != nullptr) {
//         JNIEnv* env;
//         mJVM->AttachCurrentThread(&env, nullptr);
//
//         env->DeleteGlobalRef(mJactivity);
//     }
}

void SwappyCommon::onRefreshRateChanged() {
//     JNIEnv* env;
//     mJVM->AttachCurrentThread(&env, nullptr);

    ALOGV("onRefreshRateChanged");

    SwappyCommonSettings settings;
    if (!SwappyCommonSettings::getFromApp(/*env, mJactivity,*/ &settings)) {
        ALOGE("failed to query display timings");
        return;
    }

    Settings::getInstance()->setDisplayTimings({settings.refreshPeriod,
                                                settings.appVsyncOffset,
                                                settings.sfVsyncOffset});
    ALOGV("onRefreshRateChanged: refresh rate: {:.0f}Hz",
          1e9f / settings.refreshPeriod.count());
}

nanoseconds SwappyCommon::wakeClient() {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    ++mCurrentFrame;

    // We're attempting to align with SurfaceFlinger's vsync, but it's always
    // better to be a little late than a little early (since a little early
    // could cause our frame to be picked up prematurely), so we pad by an
    // additional millisecond.
    mCurrentFrameTimestamp =
        std::chrono::steady_clock::now() + mMeasuredSwapDuration.load() + 1ms;
    mWaitingCondition.notify_all();
    return mMeasuredSwapDuration;
}

void SwappyCommon::onChoreographer(int64_t frameTimeNanos) {
//     TRACE_CALL();

    if (!mUsingExternalChoreographer) {
        mUsingExternalChoreographer = true;
        mChoreographerThread = ChoreographerThread::createChoreographerThread(
            ChoreographerThread::Type::App,
            [this] { mChoreographerFilter->onChoreographer(); },
            [this] { onRefreshRateChanged(); }, mCommonSettings.sdkVersion);
    }

    mChoreographerThread->postFrameCallbacks();
}

bool SwappyCommon::waitForNextFrame(const SwapHandlers& h) {
    int lateFrames = 0;
    bool presentationTimeIsNeeded;

    const nanoseconds cpuTime =
        (mStartFrameTime.time_since_epoch().count() == 0)
            ? 0ns
            : std::chrono::steady_clock::now() - mStartFrameTime;
//     mCPUTracer.endTrace();

    preWaitCallbacks();

    // if we are running slower than the threshold there is no point to sleep,
    // just let the app run as fast as it can
    if (mCommonSettings.refreshPeriod * mAutoSwapInterval <=
        mAutoSwapIntervalThreshold.load()) {
        waitUntilTargetFrame();

        // wait for the previous frame to be rendered
        while (!h.lastFrameIsComplete()) {
            lateFrames++;
            waitOneFrame();
        }

        mPresentationTime += lateFrames * mCommonSettings.refreshPeriod;
        presentationTimeIsNeeded = true;
    } else {
        presentationTimeIsNeeded = false;
    }

    const nanoseconds gpuTime = h.getPrevFrameGpuTime();
    addFrameDuration({cpuTime, gpuTime, mCurrentFrame > mTargetFrame});

    postWaitCallbacks(cpuTime, gpuTime);

    return presentationTimeIsNeeded;
}

void SwappyCommon::updateDisplayTimings() {
    // grab a pointer to the latest supported refresh rates
    if (mDisplayManager) {
        mSupportedRefreshPeriods =
            mDisplayManager->getSupportedRefreshPeriods();
    }

    std::lock_guard<std::mutex> lock(mMutex);
    ALOGW_ONCE_IF(!mWindow,
                  "ANativeWindow not configured, frame rate will not be "
                  "reported to Android platform");

    if (!mTimingSettingsNeedUpdate && !mWindowChanged) {
        return;
    }

    mTimingSettingsNeedUpdate = false;

    if (!mWindowChanged &&
        mCommonSettings.refreshPeriod == mNextTimingSettings.refreshPeriod &&
        mSwapDuration == mNextTimingSettings.swapDuration) {
        return;
    }

    mWindowChanged = false;
    mCommonSettings.refreshPeriod = mNextTimingSettings.refreshPeriod;

    const auto pipelineFrameTime =
        mFrameDurations.getAverageFrameTime().getTime(PipelineMode::On);
    const auto swapDuration =
        pipelineFrameTime != 0ns ? pipelineFrameTime : mSwapDuration;
    mAutoSwapInterval =
        calculateSwapInterval(swapDuration, mCommonSettings.refreshPeriod);
    mPipelineMode = PipelineMode::On;

    const bool swapIntervalValid =
        mNextTimingSettings.refreshPeriod * mAutoSwapInterval >=
        mNextTimingSettings.swapDuration;
    const bool swapIntervalChangedBySettings =
        mSwapDuration != mNextTimingSettings.swapDuration;

    mSwapDuration = mNextTimingSettings.swapDuration;
    if (!mAutoSwapIntervalEnabled || swapIntervalChangedBySettings ||
        !swapIntervalValid) {
        mAutoSwapInterval =
            calculateSwapInterval(mSwapDuration, mCommonSettings.refreshPeriod);
        mPipelineMode = PipelineMode::On;
        setPreferredRefreshPeriod(mSwapDuration);
    }

    if (mNextModeId == -1 && mLatestFrameRateVote == 0) {
        setPreferredRefreshPeriod(mSwapDuration);
    }

    mFrameDurations.clear();

    SPDLOG_TRACE("mSwapDuration {}", int(mSwapDuration.count()));
    SPDLOG_TRACE("mAutoSwapInterval {}", mAutoSwapInterval);
    SPDLOG_TRACE("mCommonSettings.refreshPeriod {}",
              mCommonSettings.refreshPeriod.count());
    SPDLOG_TRACE("mPipelineMode {}", static_cast<int>(mPipelineMode));
}

void SwappyCommon::onPreSwap(const SwapHandlers& h) {
    if (!mUsingExternalChoreographer) {
        mChoreographerThread->postFrameCallbacks();
    }

    // for non pipeline mode where both cpu and gpu work is done at the same
    // stage wait for next frame will happen after swap
    if (mPipelineMode == PipelineMode::On) {
        mPresentationTimeNeeded = waitForNextFrame(h);
    } else {
        mPresentationTimeNeeded =
            (mCommonSettings.refreshPeriod * mAutoSwapInterval <=
             mAutoSwapIntervalThreshold.load());
    }

    mSwapTime = std::chrono::steady_clock::now();
    preSwapBuffersCallbacks();
}

void SwappyCommon::onPostSwap(const SwapHandlers& h) {
    postSwapBuffersCallbacks();

    updateMeasuredSwapDuration(std::chrono::steady_clock::now() - mSwapTime);

    if (mPipelineMode == PipelineMode::Off) {
        waitForNextFrame(h);
    }

    if (updateSwapInterval()) {
        swapIntervalChangedCallbacks();
        SPDLOG_TRACE("mPipelineMode {}", static_cast<int>(mPipelineMode));
        SPDLOG_TRACE("mAutoSwapInterval {}", mAutoSwapInterval);
    }

    updateDisplayTimings();

    startFrame();
}

void SwappyCommon::updateMeasuredSwapDuration(nanoseconds duration) {
    // TODO: The exponential smoothing factor here is arbitrary
    mMeasuredSwapDuration =
        (mMeasuredSwapDuration.load() * 4 / 5) + duration / 5;

    // Clamp the swap duration to half the refresh period
    //
    // We do this since the swap duration can be a bit noisy during periods such
    // as app startup, which can cause some stuttering as the smoothing catches
    // up with the actual duration. By clamping, we reduce the maximum error
    // which reduces the calibration time.
    if (mMeasuredSwapDuration.load() > (mCommonSettings.refreshPeriod / 2)) {
        mMeasuredSwapDuration.store(mCommonSettings.refreshPeriod / 2);
    }
}

nanoseconds SwappyCommon::getSwapDuration() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mAutoSwapInterval * mCommonSettings.refreshPeriod;
};

void SwappyCommon::FrameDurations::add(FrameDuration frameDuration) {
    const auto now = std::chrono::steady_clock::now();
    mFrames.push_back({now, frameDuration});
    mFrameDurationsSum += frameDuration;
    if (frameDuration.frameMiss()) {
        mMissedFrameCount++;
    }

    while (mFrames.size() >= 2 &&
           now - (mFrames.begin() + 1)->first > FRAME_DURATION_SAMPLE_SECONDS) {
        mFrameDurationsSum -= mFrames.front().second;
        if (mFrames.front().second.frameMiss()) {
            mMissedFrameCount--;
        }
        mFrames.pop_front();
    }
}

bool SwappyCommon::FrameDurations::hasEnoughSamples() const {
    return (!mFrames.empty()) && (mFrames.back().first - mFrames.front().first >
                                  FRAME_DURATION_SAMPLE_SECONDS);
}

SwappyCommon::FrameDuration SwappyCommon::FrameDurations::getAverageFrameTime()
    const {
    if (hasEnoughSamples()) {
        return mFrameDurationsSum / mFrames.size();
    }

    return {};
}

int SwappyCommon::FrameDurations::getMissedFramePercent() const {
    return round(mMissedFrameCount * 100.0f / mFrames.size());
}

void SwappyCommon::FrameDurations::clear() {
    mFrames.clear();
    mFrameDurationsSum = {};
    mMissedFrameCount = 0;
}

void SwappyCommon::addFrameDuration(FrameDuration duration) {
    ALOGV("cpuTime = %.2f", duration.getCpuTime().count() / 1e6f);
    ALOGV("gpuTime = %.2f", duration.getGpuTime().count() / 1e6f);
    ALOGV("frame %s", duration.frameMiss() ? "MISS" : "on time");

    std::lock_guard<std::mutex> lock(mMutex);
    mFrameDurations.add(duration);
}

bool SwappyCommon::swapSlower(const FrameDuration& averageFrameTime,
                              const nanoseconds& upperBound,
                              int newSwapInterval) {
    bool swappedSlower = false;
    ALOGV("Rendering takes too much time for the given config");

    const auto frameFitsUpperBound =
        averageFrameTime.getTime(PipelineMode::On) <= upperBound;
    const auto swapDurationWithinThreshold =
        mCommonSettings.refreshPeriod * mAutoSwapInterval <=
        mAutoSwapIntervalThreshold.load() + FRAME_MARGIN;

    // Check if turning on pipeline is not enough
    if ((mPipelineMode == PipelineMode::On || !frameFitsUpperBound) &&
        swapDurationWithinThreshold) {
        int originalAutoSwapInterval = mAutoSwapInterval;
        if (newSwapInterval > mAutoSwapInterval) {
            mAutoSwapInterval = newSwapInterval;
        } else {
            mAutoSwapInterval++;
        }
        if (mAutoSwapInterval != originalAutoSwapInterval) {
            ALOGV("Changing Swap interval to %d from %d", mAutoSwapInterval,
                  originalAutoSwapInterval);
            swappedSlower = true;
        }
    }

    if (mPipelineMode == PipelineMode::Off) {
        ALOGV("turning on pipelining");
        mPipelineMode = PipelineMode::On;
    }

    return swappedSlower;
}

bool SwappyCommon::swapFaster(int newSwapInterval) {
    bool swappedFaster = false;
    int originalAutoSwapInterval = mAutoSwapInterval;
    while (newSwapInterval < mAutoSwapInterval && swapFasterCondition()) {
        mAutoSwapInterval--;
    }

    if (mAutoSwapInterval != originalAutoSwapInterval) {
        ALOGV("Rendering is much shorter for the given config");
        ALOGV("Changing Swap interval to %d from %d", mAutoSwapInterval,
              originalAutoSwapInterval);
        // since we changed the swap interval, we may need to turn on pipeline
        // mode
        ALOGV("Turning on pipelining");
        mPipelineMode = PipelineMode::On;
        swappedFaster = true;
    }

    return swappedFaster;
}

bool SwappyCommon::updateSwapInterval() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mAutoSwapIntervalEnabled) return false;

    if (!mFrameDurations.hasEnoughSamples()) return false;

    const auto averageFrameTime = mFrameDurations.getAverageFrameTime();
    const auto pipelineFrameTime = averageFrameTime.getTime(PipelineMode::On);
    const auto nonPipelineFrameTime =
        averageFrameTime.getTime(PipelineMode::Off);

    // calculate the new swap interval based on average frame time assume we are
    // in pipeline mode (prefer higher swap interval rather than turning off
    // pipeline mode)
    const int newSwapInterval =
        calculateSwapInterval(pipelineFrameTime, mCommonSettings.refreshPeriod);

    // Define upper and lower bounds based on the swap duration
    const nanoseconds upperBoundForThisRefresh =
        mCommonSettings.refreshPeriod * mAutoSwapInterval;
    const nanoseconds lowerBoundForThisRefresh =
        mCommonSettings.refreshPeriod * (mAutoSwapInterval - 1) - FRAME_MARGIN;

    const int missedFramesPercent = mFrameDurations.getMissedFramePercent();

    ALOGV("mPipelineMode = %d", static_cast<int>(mPipelineMode));
    ALOGV("Average cpu frame time = %.2f",
          (averageFrameTime.getCpuTime().count()) / 1e6f);
    ALOGV("Average gpu frame time = %.2f",
          (averageFrameTime.getGpuTime().count()) / 1e6f);
    ALOGV("upperBound = %.2f", upperBoundForThisRefresh.count() / 1e6f);
    ALOGV("lowerBound = %.2f", lowerBoundForThisRefresh.count() / 1e6f);
    ALOGV("frame missed = %d%%", missedFramesPercent);

    bool configChanged = false;
    ALOGV("pipelineFrameTime = %.2f", pipelineFrameTime.count() / 1e6f);
    const auto nonPipelinePercent = (100.f + NON_PIPELINE_PERCENT) / 100.f;

    // Make sure the frame time fits in the current config to avoid missing
    // frames
    if (missedFramesPercent > FRAME_DROP_THRESHOLD) {
        if (swapSlower(averageFrameTime, upperBoundForThisRefresh,
                       newSwapInterval))
            configChanged = true;
    }

    // So we shouldn't miss any frames with this config but maybe we can go
    // faster ? we check the pipeline frame time here as we prefer lower swap
    // interval than no pipelining
    else if (missedFramesPercent == 0 && swapFasterCondition() &&
             pipelineFrameTime < lowerBoundForThisRefresh) {
        if (swapFaster(newSwapInterval)) configChanged = true;
    }

    // If we reached to this condition it means that we fit into the boundaries.
    // However we might be in pipeline mode and we could turn it off if we still
    // fit. To be very conservative, switch to non-pipeline if frame time * 50%
    // fits
    else if (mPipelineModeAutoMode && mPipelineMode == PipelineMode::On &&
             nonPipelineFrameTime * nonPipelinePercent <
                 upperBoundForThisRefresh) {
        ALOGV(
            "Rendering time fits the current swap interval without pipelining");
        mPipelineMode = PipelineMode::Off;
        configChanged = true;
    }

    if (configChanged) {
        mFrameDurations.clear();
    }

    setPreferredRefreshPeriod(pipelineFrameTime);

    return configChanged;
}

template <typename Tracers, typename Func>
void addToTracers(Tracers& tracers, Func func, void* userData) {
    if (func != nullptr) {
        tracers.push_back(
            [func, userData](auto... params) { func(userData, params...); });
    }
}

void SwappyCommon::addTracerCallbacks(SwappyTracer tracer) {
    addToTracers(mInjectedTracers.preWait, tracer.preWait, tracer.userData);
    addToTracers(mInjectedTracers.postWait, tracer.postWait, tracer.userData);
    addToTracers(mInjectedTracers.preSwapBuffers, tracer.preSwapBuffers,
                 tracer.userData);
    addToTracers(mInjectedTracers.postSwapBuffers, tracer.postSwapBuffers,
                 tracer.userData);
    addToTracers(mInjectedTracers.startFrame, tracer.startFrame,
                 tracer.userData);
    addToTracers(mInjectedTracers.swapIntervalChanged,
                 tracer.swapIntervalChanged, tracer.userData);
}

template <typename T, typename... Args>
void executeTracers(T& tracers, Args... args) {
    for (const auto& tracer : tracers) {
        tracer(std::forward<Args>(args)...);
    }
}

void SwappyCommon::preSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.preSwapBuffers);
}

void SwappyCommon::postSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.postSwapBuffers,
                   (int64_t)mPresentationTime.time_since_epoch().count());
}

void SwappyCommon::preWaitCallbacks() {
    executeTracers(mInjectedTracers.preWait);
}

void SwappyCommon::postWaitCallbacks(nanoseconds cpuTime, nanoseconds gpuTime) {
    executeTracers(mInjectedTracers.postWait, cpuTime.count(), gpuTime.count());
}

void SwappyCommon::startFrameCallbacks() {
    executeTracers(mInjectedTracers.startFrame, mCurrentFrame,
                   (int64_t)mPresentationTime.time_since_epoch().count());
}

void SwappyCommon::swapIntervalChangedCallbacks() {
    executeTracers(mInjectedTracers.swapIntervalChanged);
}

void SwappyCommon::setAutoSwapInterval(bool enabled) {
    std::lock_guard<std::mutex> lock(mMutex);
    mAutoSwapIntervalEnabled = enabled;

    // non pipeline mode is not supported when auto mode is disabled
    if (!enabled) {
        mPipelineMode = PipelineMode::On;
//         TRACE_INT("mPipelineMode", static_cast<int>(mPipelineMode));
    }
}

void SwappyCommon::setAutoPipelineMode(bool enabled) {
    std::lock_guard<std::mutex> lock(mMutex);
    mPipelineModeAutoMode = enabled;
//     TRACE_INT("mPipelineModeAutoMode", mPipelineModeAutoMode);
    if (!enabled) {
        mPipelineMode = PipelineMode::On;
//         TRACE_INT("mPipelineMode", static_cast<int>(mPipelineMode));
    }
}

void SwappyCommon::setPreferredDisplayModeId(int modeId) {
    if (!mDisplayManager || modeId < 0 || mNextModeId == modeId) {
        return;
    }

    mNextModeId = modeId;
    mDisplayManager->setPreferredDisplayModeId(modeId);
    ALOGV("setPreferredDisplayModeId set to %d", modeId);
}

int SwappyCommon::calculateSwapInterval(nanoseconds frameTime,
                                        nanoseconds refreshPeriod) {
    if (frameTime < refreshPeriod) {
        return 1;
    }

    auto div_result = div(frameTime.count(), refreshPeriod.count());
    auto framesPerRefresh = div_result.quot;
    auto framesPerRefreshRemainder = div_result.rem;

    return (framesPerRefresh +
            (framesPerRefreshRemainder > REFRESH_RATE_MARGIN.count() ? 1 : 0));
}

void SwappyCommon::setPreferredRefreshPeriod(nanoseconds frameTime) {
    if (mANativeWindow_setFrameRate && mWindow) {
        auto frameRate = 1e9f / frameTime.count();

        frameRate = std::min(frameRate, 1e9f / (mSwapDuration).count());
        if (std::abs(mLatestFrameRateVote - frameRate) >
            FRAME_RATE_VOTE_MARGIN) {
            mLatestFrameRateVote = frameRate;
            ALOGV("ANativeWindow_setFrameRate(%.2f)", frameRate);
            mANativeWindow_setFrameRate(
                mWindow, frameRate,
                ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
        }

//         TRACE_INT("preferredRefreshPeriod", (int)frameRate);
    } else {
        if (!mDisplayManager || !mSupportedRefreshPeriods) {
            return;
        }
        // Loop across all supported refresh periods to find the best refresh
        // period. Best refresh period means:
        //      Shortest swap period that can still accommodate the frame time
        //      and that has the longest refresh period possible to optimize
        //      power consumption.
        std::pair<nanoseconds, int> bestRefreshConfig;
        nanoseconds minSwapDuration = 1s;
        for (const auto& refreshConfig : *mSupportedRefreshPeriods) {
            const auto period = refreshConfig.first;
            const int swapIntervalForPeriod =
                calculateSwapInterval(frameTime, period);
            const nanoseconds swapDuration = period * swapIntervalForPeriod;

            // Don't allow swapping faster than mSwapDuration (see public
            // header)
            if (swapDuration + FRAME_MARGIN < mSwapDuration) {
                continue;
            }

            // We iterate in ascending order of refresh period, so accepting any
            // better or equal-within-margin duration here chooses the longest
            // refresh period possible.
            if (swapDuration < minSwapDuration + FRAME_MARGIN) {
                minSwapDuration = swapDuration;
                bestRefreshConfig = refreshConfig;
            }
        }

        // Switch if we have a potentially better refresh rate
        {
//             TRACE_INT("preferredRefreshPeriod",
//                       bestRefreshConfig.first.count());
            setPreferredDisplayModeId(bestRefreshConfig.second);
        }
    }
}

void SwappyCommon::onSettingsChanged() {
    std::lock_guard<std::mutex> lock(mMutex);

    TimingSettings timingSettings =
        TimingSettings::from(*Settings::getInstance());

    // If display timings has changed, cache the update and apply them on the
    // next frame
    if (timingSettings != mNextTimingSettings) {
        mNextTimingSettings = timingSettings;
        mTimingSettingsNeedUpdate = true;
    }
}

void SwappyCommon::startFrame() {
//     TRACE_CALL();

    int32_t currentFrame;
    std::chrono::steady_clock::time_point currentFrameTimestamp;
    {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        currentFrame = mCurrentFrame;
        currentFrameTimestamp = mCurrentFrameTimestamp;
    }

    // Whether to add a wait to fix buffer stuffing.
    bool waitFrame = false;

    const int intervals = (mPipelineMode == PipelineMode::On) ? 2 : 1;

    // Use frame statistics to fix any buffer stuffing
    if (mBufferStuffingFixWait > 0 && mFrameStatistics) {
        int32_t lastLatency = mFrameStatistics->lastLatencyRecorded();
        int expectedLatency = mAutoSwapInterval * intervals;
//         TRACE_INT("ExpectedLatency", expectedLatency);
        if (mBufferStuffingFixCounter == 0) {
            if (lastLatency > expectedLatency) {
                mMissedFrameCounter++;
                if (mMissedFrameCounter >= mBufferStuffingFixWait) {
                    waitFrame = true;
                    mBufferStuffingFixCounter = 2 * lastLatency;
                    SPDLOG_TRACE("BufferStuffingFix {}", mBufferStuffingFixCounter);
                }
            } else {
                mMissedFrameCounter = 0;
            }
        } else {
            --mBufferStuffingFixCounter;
            SPDLOG_TRACE("BufferStuffingFix {}", mBufferStuffingFixCounter);
        }
    }
    mTargetFrame = currentFrame + mAutoSwapInterval;
    if (waitFrame) mTargetFrame += 1;

    // We compute the target time as now
    //   + the time the buffer will be on the GPU and in the queue to the
    //   compositor (1 swap period)
    mPresentationTime =
        currentFrameTimestamp +
        (mAutoSwapInterval * intervals) * mCommonSettings.refreshPeriod;

    mStartFrameTime = std::chrono::steady_clock::now();
//     mCPUTracer.startTrace();

    startFrameCallbacks();
}

void SwappyCommon::waitUntil(int32_t target) {
//     TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    mWaitingCondition.wait(lock, [&]() {
        if (mCurrentFrame < target) {
            if (!mUsingExternalChoreographer) {
                mChoreographerThread->postFrameCallbacks();
            }
            return false;
        }
        return true;
    });
}

void SwappyCommon::waitUntilTargetFrame() { waitUntil(mTargetFrame); }

void SwappyCommon::waitOneFrame() { waitUntil(mCurrentFrame + 1); }

SdkVersion SwappyCommonSettings::getSDKVersion(/*JNIEnv* env*/) {
    return SdkVersion{1, 0};
}

void SwappyCommon::setANativeWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mWindow == window) {
        return;
    }

    if (mWindow != nullptr) {
//         ANativeWindow_release(mWindow);
    }

    mWindow = window;
    if (mWindow != nullptr) {
//         ANativeWindow_acquire(mWindow);
        mWindowChanged = true;
        mLatestFrameRateVote = 0;
    }
}

namespace {

struct DeviceIdentifier {
    std::string manufacturer;
    std::string model;
    std::string display;
    // Empty fields match against any value and we match the beginning of the
    // input, e.g.
    //  A37 matches A37f, A37fw, etc.
    bool match(const std::string& manufacturer_in, const std::string& model_in,
               const std::string& display_in) {
        if (!matchStartOfString(manufacturer, manufacturer_in)) return false;
        if (!matchStartOfString(model, model_in)) return false;
        if (!matchStartOfString(display, display_in)) return false;
        return true;
    }
    bool matchStartOfString(const std::string& start,
                            const std::string& sample) {
        return start.empty() || start == sample.substr(0, start.length());
    }
};

}  // anonymous namespace

bool SwappyCommon::isDeviceUnsupported() {
    return false;
}

}  // namespace swappy
