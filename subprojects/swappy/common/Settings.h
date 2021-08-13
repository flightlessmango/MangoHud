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

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Thread.h"

namespace swappy {

class Settings {
   private:
    // Allows construction with std::unique_ptr from a static method, but
    // disallows construction outside of the class since no one else can
    // construct a ConstructorTag
    struct ConstructorTag {};

   public:
    struct DisplayTimings {
        std::chrono::nanoseconds refreshPeriod{0};
        std::chrono::nanoseconds appOffset{0};
        std::chrono::nanoseconds sfOffset{0};
    };

    explicit Settings(ConstructorTag){};

    static Settings* getInstance();

    static void reset();

    using Listener = std::function<void()>;
    void addListener(Listener listener);

    void setDisplayTimings(const DisplayTimings& displayTimings);
    void setSwapDuration(uint64_t swapNs);
    void setUseAffinity(bool);

    const DisplayTimings& getDisplayTimings() const;
    std::chrono::nanoseconds getSwapDuration() const;
    bool getUseAffinity() const;

   private:
    void notifyListeners();

    static std::unique_ptr<Settings> instance;

    mutable std::mutex mMutex;
    std::vector<Listener> mListeners GUARDED_BY(mMutex);

    DisplayTimings mDisplayTimings GUARDED_BY(mMutex);
    std::chrono::nanoseconds mSwapDuration GUARDED_BY(mMutex) =
        std::chrono::nanoseconds(16'666'667L);
    bool mUseAffinity GUARDED_BY(mMutex) = true;
};

}  // namespace swappy
