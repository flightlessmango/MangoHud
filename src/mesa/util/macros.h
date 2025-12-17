// /*
//  * Copyright © 2014 Intel Corporation
//  *
//  * Permission is hereby granted, free of charge, to any person obtaining a
//  * copy of this software and associated documentation files (the "Software"),
//  * to deal in the Software without restriction, including without limitation
//  * the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  * and/or sell copies of the Software, and to permit persons to whom the
//  * Software is furnished to do so, subject to the following conditions:
//  *
//  * The above copyright notice and this permission notice (including the next
//  * paragraph) shall be included in all copies or substantial portions of the
//  * Software.
//  *
//  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
//  * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
//  * IN THE SOFTWARE.
//  */

#ifndef UTIL_MACROS_H
#define UTIL_MACROS_H

#include <assert.h>
#include "../c99_compat.h"
#include "../c11_compat.h"
#if defined(__HAIKU__)  && !defined(__cplusplus)
#define static_assert _Static_assert
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// /* Compute the size of an array */
#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#if defined(HAVE___BUILTIN_UNREACHABLE) || __has_builtin(__builtin_unreachable)
#define UNREACHABLE(str)    \
do {                        \
   (void)"" str; /* str must be a string literal */ \
   assert(!str);            \
   __builtin_unreachable(); \
} while (0)
#elif defined (_MSC_VER)
#define UNREACHABLE(str)    \
do {                        \
   (void)"" str; /* str must be a string literal */ \
   assert(!str);            \
   __assume(0);             \
} while (0)
#else
#define UNREACHABLE(str)    \
do {                        \
   (void)"" str; /* str must be a string literal */ \
   assert(!str);            \
} while (0)
#endif

#define PUBLIC __attribute__((visibility("default")))
#endif /* UTIL_MACROS_H */
