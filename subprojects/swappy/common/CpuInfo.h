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

#include <sched.h>

#include <map>
#include <string>
#include <vector>

namespace swappy {

class CpuInfo {
   public:
    struct Cpu {
        enum class Type { Little, Big };

        int id;
        int package_id;
        long frequency;

        Type type;
    };

    CpuInfo();

    unsigned int getNumberOfCpus() const;

    const std::vector<Cpu>& getCpus() const;
    const std::string getHardware() const;

    unsigned int getNumberOfLittleCores() const;
    unsigned int getNumberOfBigCores() const;

    cpu_set_t getLittleCoresMask() const;
    cpu_set_t getBigCoresMask() const;

   private:
    std::vector<Cpu> mCpus;
    std::string mHardware;

    unsigned int mNumberOfLittleCores = 0;
    unsigned int mNumberOfBigCores = 0;

    cpu_set_t mLittleCoresMask;
    cpu_set_t mBigCoresMask;
};

unsigned int to_mask(cpu_set_t cpu_set);

}  // namespace swappy
