/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef VK_UTIL_H
#define VK_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

/* common inlines and macros for vulkan drivers */

#include <vulkan/vulkan.h>

#define vk_foreach_struct(__iter, __start) \
   for (struct VkBaseOutStructure *__iter = (struct VkBaseOutStructure *)(__start); \
        __iter; __iter = __iter->pNext)

#define vk_foreach_struct_const(__iter, __start) \
   for (const struct VkBaseInStructure *__iter = (const struct VkBaseInStructure *)(__start); \
        __iter; __iter = __iter->pNext)

/**
 * A wrapper for a Vulkan output array. A Vulkan output array is one that
 * follows the convention of the parameters to
 * vkGetPhysicalDeviceQueueFamilyProperties().
 *
 * Example Usage:
 *
 *    VkResult
 *    vkGetPhysicalDeviceQueueFamilyProperties(
 *       VkPhysicalDevice           physicalDevice,
 *       uint32_t*                  pQueueFamilyPropertyCount,
 *       VkQueueFamilyProperties*   pQueueFamilyProperties)
 *    {
 *       VK_OUTARRAY_MAKE(props, pQueueFamilyProperties,
 *                         pQueueFamilyPropertyCount);
 *
 *       vk_outarray_append(&props, p) {
 *          p->queueFlags = ...;
 *          p->queueCount = ...;
 *       }
 *
 *       vk_outarray_append(&props, p) {
 *          p->queueFlags = ...;
 *          p->queueCount = ...;
 *       }
 *
 *       return vk_outarray_status(&props);
 *    }
 */
struct __vk_outarray {
   /** May be null. */
   void *data;

   /**
    * Capacity, in number of elements. Capacity is unlimited (UINT32_MAX) if
    * data is null.
    */
   uint32_t cap;

   /**
    * Count of elements successfully written to the array. Every write is
    * considered successful if data is null.
    */
   uint32_t *filled_len;

   /**
    * Count of elements that would have been written to the array if its
    * capacity were sufficient. Vulkan functions often return VK_INCOMPLETE
    * when `*filled_len < wanted_len`.
    */
   uint32_t wanted_len;
};

static inline void
__vk_outarray_init(struct __vk_outarray *a,
                   void *data, uint32_t *restrict len)
{
   a->data = data;
   a->cap = *len;
   a->filled_len = len;
   *a->filled_len = 0;
   a->wanted_len = 0;

   if (a->data == NULL)
      a->cap = UINT32_MAX;
}

static inline VkResult
__vk_outarray_status(const struct __vk_outarray *a)
{
   if (*a->filled_len < a->wanted_len)
      return VK_INCOMPLETE;
   else
      return VK_SUCCESS;
}

static inline void *
__vk_outarray_next(struct __vk_outarray *a, size_t elem_size)
{
   void *p = NULL;

   a->wanted_len += 1;

   if (*a->filled_len >= a->cap)
      return NULL;

   if (a->data != NULL)
      p = (uint8_t *)a->data + (*a->filled_len) * elem_size;

   *a->filled_len += 1;

   return p;
}

#define vk_outarray(elem_t) \
   struct { \
      struct __vk_outarray base; \
      elem_t meta[]; \
   }

#define vk_outarray_typeof_elem(a) __typeof__((a)->meta[0])
#define vk_outarray_sizeof_elem(a) sizeof((a)->meta[0])

#define vk_outarray_init(a, data, len) \
   __vk_outarray_init(&(a)->base, (data), (len))

#define VK_OUTARRAY_MAKE(name, data, len) \
   vk_outarray(__typeof__((data)[0])) name; \
   vk_outarray_init(&name, (data), (len))

#define vk_outarray_status(a) \
   __vk_outarray_status(&(a)->base)

#define vk_outarray_next(a) \
   ((vk_outarray_typeof_elem(a) *) \
      __vk_outarray_next(&(a)->base, vk_outarray_sizeof_elem(a)))

/**
 * Append to a Vulkan output array.
 *
 * This is a block-based macro. For example:
 *
 *    vk_outarray_append(&a, elem) {
 *       elem->foo = ...;
 *       elem->bar = ...;
 *    }
 *
 * The array `a` has type `vk_outarray(elem_t) *`. It is usually declared with
 * VK_OUTARRAY_MAKE(). The variable `elem` is block-scoped and has type
 * `elem_t *`.
 *
 * The macro unconditionally increments the array's `wanted_len`. If the array
 * is not full, then the macro also increment its `filled_len` and then
 * executes the block. When the block is executed, `elem` is non-null and
 * points to the newly appended element.
 */
#define vk_outarray_append(a, elem) \
   for (vk_outarray_typeof_elem(a) *elem = vk_outarray_next(a); \
        elem != NULL; elem = NULL)

static inline void *
__vk_find_struct(void *start, VkStructureType sType)
{
   vk_foreach_struct(s, start) {
      if (s->sType == sType)
         return s;
   }

   return NULL;
}

#define vk_find_struct(__start, __sType) \
   __vk_find_struct((__start), VK_STRUCTURE_TYPE_##__sType)

#define vk_find_struct_const(__start, __sType) \
   (const void *)__vk_find_struct((void *)(__start), VK_STRUCTURE_TYPE_##__sType)

static inline void
__vk_append_struct(void *start, void *element)
{
   vk_foreach_struct(s, start) {
      if (s->pNext)
         continue;

      s->pNext = (struct VkBaseOutStructure *) element;
      break;
   }
}

uint32_t vk_get_driver_version(void);

uint32_t vk_get_version_override(void);

#define VK_EXT_OFFSET (1000000000UL)
#define VK_ENUM_EXTENSION(__enum) \
   ((__enum) >= VK_EXT_OFFSET ? ((((__enum) - VK_EXT_OFFSET) / 1000UL) + 1) : 0)
#define VK_ENUM_OFFSET(__enum) \
   ((__enum) >= VK_EXT_OFFSET ? ((__enum) % 1000) : (__enum))

#ifdef __cplusplus
}
#endif

#endif /* VK_UTIL_H */
