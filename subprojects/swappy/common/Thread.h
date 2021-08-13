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

#include <cstdint>
#include <functional>
#include <memory>

// Enable thread safety attributes only with clang.
// The attributes can be safely erased when compiling with other compilers.
#if defined(__clang__) && (!defined(SWIG))
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)  // no-op
#endif

#if !defined GAMESDK_THREAD_CHECKS
#define GAMESDK_THREAD_CHECKS 1
#endif

#if GAMESDK_THREAD_CHECKS
#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define REQUIRES(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define NO_THREAD_SAFETY_ANALYSIS \
    THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)
#else
#define GUARDED_BY(x)
#define REQUIRES(...)
#define NO_THREAD_SAFETY_ANALYSIS
#endif

namespace swappy {

enum class Affinity { None_, Even, Odd };

int32_t getNumCpus();
void setAffinity(int32_t cpu);
void setAffinity(Affinity affinity);

struct ThreadImpl;

class Thread {
    std::unique_ptr<ThreadImpl> impl_;

   public:
    Thread() noexcept;
    Thread(std::function<void()>&& fn) noexcept;
    Thread(Thread&& rhs) noexcept;
    Thread(const Thread&) = delete;
    Thread& operator=(Thread&& rhs) noexcept;
    Thread& operator=(const Thread& rhs) = delete;
    ~Thread();
    void join();
    bool joinable();
};

}  // namespace swappy
