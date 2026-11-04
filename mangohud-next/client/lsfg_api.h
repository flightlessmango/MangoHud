/*
 * api.h - Public C ABI for lsfg-vk-layer.
 *
 * MIT LICENSE
 *
 * Copyright (c) 2026 PancakeTAS
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LSFGVK_API_H
#define LSFGVK_API_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LSFGVK_STRUCTURE_TYPE_CREATE_QUERY = 0x4C530000,
    LSFGVK_STRUCTURE_TYPE_CREATE_INFO  = 0x4C530001,
    LSFGVK_STRUCTURE_TYPE_PRESENT_QUERY = 0x4C530002,
    LSFGVK_STRUCTURE_TYPE_PRESENT_INFO  = 0x4C530003,
    LSFGVK_STRUCTURE_TYPE_MAX_ENUM = 0x7FFFFFFF
} LSFGVKStructureType;

typedef struct {
    bool isPresent;
    bool requiresVsync;
} LSFGVKMangoHudInfo;

typedef struct {
    LSFGVKStructureType sType;
    void* pNext;
    LSFGVKMangoHudInfo* mangohud_info;
} LSFGVKCreateQuery;

typedef struct {
    LSFGVKStructureType sType;
    const void* pNext;
    const LSFGVKMangoHudInfo* mangohud_info;
} LSFGVKCreateInfo;

typedef struct {
    uint32_t presentTimeCount;
    uint64_t* pTimeDomainIds;
    VkPresentStageFlagsEXT* pPresentStages;
} LSFGVKMangoHudPresentQuery;

typedef struct {
    LSFGVKStructureType sType;
    void* pNext;
    uint32_t presentCount;
    uint32_t* pPresents;
    LSFGVKMangoHudPresentQuery* mangohud_present_query;
} LSFGVKPresentQuery;

typedef struct {
    LSFGVKStructureType sType;
    const void* pNext;
    uint32_t presentIndexCount;
    uint32_t* pPresentIndex;
} LSFGVKPresentInfo;

#ifdef __cplusplus
}
#endif

#endif
