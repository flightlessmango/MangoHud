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

#include <spdlog/spdlog.h>

#include <string>

#define ALOGE(...) SPDLOG_ERROR(__VA_ARGS__)
#define ALOGW(...) SPDLOG_WARN(__VA_ARGS__)
#define ALOGI(...) SPDLOG_INFO(__VA_ARGS__)
#define ALOGW_ONCE_IF(cond, ...)                               \
    do {                                                       \
        static bool alogw_once##__FILE__##__LINE__##__ = true; \
        if (cond && alogw_once##__FILE__##__LINE__##__) {      \
            alogw_once##__FILE__##__LINE__##__ = false;        \
            ALOGW(__VA_ARGS__);                                \
        }                                                      \
    } while (0)
#define ALOGE_ONCE(...)                                        \
    do {                                                       \
        static bool aloge_once##__FILE__##__LINE__##__ = true; \
        if (aloge_once##__FILE__##__LINE__##__) {              \
            aloge_once##__FILE__##__LINE__##__ = false;        \
            ALOGE(__VA_ARGS__);                                \
        }                                                      \
    } while (0)

#ifndef NDEBUG
#define ALOGV(...) \
    SPDLOG_TRACE(__VA_ARGS__)
#else
#define ALOGV(...)
#endif

namespace swappy {

std::string to_string(int value);

}
