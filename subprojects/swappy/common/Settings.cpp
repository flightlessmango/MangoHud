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

#include "Settings.h"

#define LOG_TAG "Settings"

//#include "Log.h"

namespace swappy {

std::unique_ptr<Settings> Settings::instance;

Settings* Settings::getInstance() {
    if (!instance) {
        instance = std::make_unique<Settings>(ConstructorTag{});
    }
    return instance.get();
}

void Settings::reset() { instance.reset(); }

void Settings::addListener(Listener listener) {
    std::lock_guard<std::mutex> lock(mMutex);
    mListeners.emplace_back(std::move(listener));
}

void Settings::setDisplayTimings(const DisplayTimings& displayTimings) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mDisplayTimings = displayTimings;
    }
    // Notify the listeners without the lock held
    notifyListeners();
}
void Settings::setSwapDuration(uint64_t swapNs) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mSwapDuration = std::chrono::nanoseconds(swapNs);
    }
    // Notify the listeners without the lock held
    notifyListeners();
}

void Settings::setUseAffinity(bool tf) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mUseAffinity = tf;
    }
    // Notify the listeners without the lock held
    notifyListeners();
}

const Settings::DisplayTimings& Settings::getDisplayTimings() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mDisplayTimings;
}

std::chrono::nanoseconds Settings::getSwapDuration() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSwapDuration;
}

bool Settings::getUseAffinity() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mUseAffinity;
}

void Settings::notifyListeners() {
    // Grab a local copy of the listeners
    std::vector<Listener> listeners;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        listeners = mListeners;
    }

    // Call the listeners without the lock held
    for (const auto& listener : listeners) {
        listener();
    }
}

}  // namespace swappy
