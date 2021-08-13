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

#include "Thread.h"

#include <sched.h>
#include <unistd.h>

#include <thread>

#include "swappy/swappy_common.h"

#define LOG_TAG "SwappyThread"
#include "Log.h"

namespace swappy {

int32_t getNumCpus() {
    static int32_t sNumCpus = []() {
        pid_t pid = gettid();
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        sched_getaffinity(pid, sizeof(cpuSet), &cpuSet);

        int32_t numCpus = 0;
        while (CPU_ISSET(numCpus, &cpuSet)) {
            ++numCpus;
        }

        return numCpus;
    }();

    return sNumCpus;
}

void setAffinity(int32_t cpu) {
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(cpu, &cpuSet);
    sched_setaffinity(gettid(), sizeof(cpuSet), &cpuSet);
}

void setAffinity(Affinity affinity) {
    const int32_t numCpus = getNumCpus();

    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    for (int32_t cpu = 0; cpu < numCpus; ++cpu) {
        switch (affinity) {
            case Affinity::None_:
                CPU_SET(cpu, &cpuSet);
                break;
            case Affinity::Even:
                if (cpu % 2 == 0) CPU_SET(cpu, &cpuSet);
                break;
            case Affinity::Odd:
                if (cpu % 2 == 1) CPU_SET(cpu, &cpuSet);
                break;
        }
    }

    sched_setaffinity(gettid(), sizeof(cpuSet), &cpuSet);
}

static const SwappyThreadFunctions* s_ext_thread_manager = nullptr;

struct ThreadImpl {
    virtual ~ThreadImpl() {}
    virtual bool joinable() = 0;
    virtual void join() = 0;
};

struct ExtThreadImpl : public ThreadImpl {
    std::function<void()> fn_;
    SwappyThreadId id_;

   public:
    ExtThreadImpl(std::function<void()>&& fn) : fn_(std::move(fn)) {
        if (s_ext_thread_manager->start(&id_, startThread, this) != 0) {
            ALOGE("Couldn't create thread");
        }
    }
    void join() { s_ext_thread_manager->join(id_); }
    bool joinable() { return s_ext_thread_manager->joinable(id_); }
    static void* startThread(void* x) {
        ExtThreadImpl* impl = (ExtThreadImpl*)x;
        impl->fn_();
        return nullptr;
    }
};

struct StlThreadImpl : public ThreadImpl {
    std::thread thread_;

   public:
    StlThreadImpl(std::function<void()>&& fn) : thread_(std::move(fn)) {}
    void join() { thread_.join(); }
    bool joinable() { return thread_.joinable(); }
};

Thread::Thread() noexcept {}

Thread::~Thread() {}

Thread::Thread(std::function<void()>&& fn) noexcept {
    if (s_ext_thread_manager != nullptr) {
        impl_ = std::make_unique<ExtThreadImpl>(std::move(fn));
    } else {
        impl_ = std::make_unique<StlThreadImpl>(std::move(fn));
    }
}

Thread::Thread(Thread&& rhs) noexcept : impl_(std::move(rhs.impl_)) {}

Thread& Thread::operator=(Thread&& rhs) noexcept {
    if (&rhs != this) {
        impl_ = std::move(rhs.impl_);
    }
    return *this;
}

void Thread::join() {
    if (impl_.get()) {
        impl_->join();
    }
}

bool Thread::joinable() {
    return (impl_.get() != nullptr && impl_->joinable());
}

}  // namespace swappy

extern "C" void Swappy_setThreadFunctions(const SwappyThreadFunctions* mgr) {
    swappy::s_ext_thread_manager = mgr;
}
