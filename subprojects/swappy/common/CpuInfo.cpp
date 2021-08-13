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

#define LOG_TAG "Swappy"

#include "CpuInfo.h"

#include <bitset>
#include <cstdlib>
#include <cstring>
#include <limits>

//#include "Log.h"

namespace {

bool startsWith(std::string &mainStr, const char *toMatch) {
    // std::string::find returns 0 if toMatch is found at beginning
    return mainStr.find(toMatch) == 0;
}

std::vector<std::string> split(const std::string &s, char c) {
    std::vector<std::string> v;
    std::string::size_type i = 0;
    std::string::size_type j = s.find(c);

    while (j != std::string::npos) {
        v.push_back(s.substr(i, j - i));
        i = ++j;
        j = s.find(c, j);

        if (j == std::string::npos) {
            v.push_back(s.substr(i, s.length()));
        }
    }
    return v;
}

std::string ReadFile(const std::string &path) {
    char buf[10240];
    FILE *fp = fopen(path.c_str(), "r");
    if (fp == nullptr) return std::string();

    fgets(buf, 10240, fp);
    fclose(fp);
    return std::string(buf);
}

}  // anonymous namespace

namespace swappy {

std::string to_string(int n) {
    constexpr int kBufSize = 12;  // strlen("−2147483648")+1
    static char buf[kBufSize];
    snprintf(buf, kBufSize, "%d", n);
    return buf;
}

CpuInfo::CpuInfo() {
    const auto BUFFER_LENGTH = 10240;

    char buf[BUFFER_LENGTH];
    FILE *fp = fopen("/proc/cpuinfo", "r");

    if (!fp) {
        return;
    }

    long mMaxFrequency = 0;
    long mMinFrequency = std::numeric_limits<long>::max();

    while (fgets(buf, BUFFER_LENGTH, fp) != NULL) {
        buf[strlen(buf) - 1] = '\0';  // eat the newline fgets() stores
        std::string line = buf;

        if (startsWith(line, "processor")) {
            Cpu core;
            core.id = mCpus.size();

            auto core_path =
                std::string("/sys/devices/system/cpu/cpu") + to_string(core.id);

            auto package_id =
                ReadFile(core_path + "/topology/physical_package_id");
            auto frequency = ReadFile(core_path + "/cpufreq/cpuinfo_max_freq");

            core.package_id = atol(package_id.c_str());
            core.frequency = atol(frequency.c_str());

            mMinFrequency = std::min(mMinFrequency, core.frequency);
            mMaxFrequency = std::max(mMaxFrequency, core.frequency);

            mCpus.push_back(core);
        } else if (startsWith(line, "Hardware")) {
            mHardware = split(line, ':')[1];
        }
    }
    fclose(fp);

    CPU_ZERO(&mLittleCoresMask);
    CPU_ZERO(&mBigCoresMask);

    for (auto cpu : mCpus) {
        if (cpu.frequency == mMinFrequency) {
            ++mNumberOfLittleCores;
            cpu.type = Cpu::Type::Little;
            CPU_SET(cpu.id, &mLittleCoresMask);
        } else {
            ++mNumberOfBigCores;
            cpu.type = Cpu::Type::Big;
            CPU_SET(cpu.id, &mBigCoresMask);
        }
    }
}

unsigned int CpuInfo::getNumberOfCpus() const { return mCpus.size(); }

const std::vector<CpuInfo::Cpu> &CpuInfo::getCpus() const { return mCpus; }

const std::string CpuInfo::getHardware() const { return mHardware; }

unsigned int CpuInfo::getNumberOfLittleCores() const {
    return mNumberOfLittleCores;
}

unsigned int CpuInfo::getNumberOfBigCores() const { return mNumberOfBigCores; }

cpu_set_t CpuInfo::getLittleCoresMask() const { return mLittleCoresMask; }

cpu_set_t CpuInfo::getBigCoresMask() const { return mBigCoresMask; }

unsigned int to_mask(cpu_set_t cpu_set) {
    std::bitset<32> mask;

    for (int i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &cpu_set)) mask[i] = 1;
    }
    return (int)mask.to_ulong();
}

}  // namespace swappy
