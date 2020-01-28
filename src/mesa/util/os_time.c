/**************************************************************************
 *
 * Copyright 2008-2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * @file
 * OS independent time-manipulation functions.
 * 
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include "os_time.h"
#include "detect_os.h"

#if defined(USE_GCC_ATOMIC_BUILTINS)
/* The builtins with explicit memory model are available since GCC 4.7. */
#define p_atomic_read(_v) __atomic_load_n((_v), __ATOMIC_ACQUIRE)
#else
#define p_atomic_read(_v) (*(_v))
#endif

#if DETECT_OS_UNIX
#  include <unistd.h> /* usleep */
#  include <time.h> /* timeval */
#  include <sys/time.h> /* timeval */
#  include <sched.h> /* sched_yield */
#  include <errno.h>
#elif DETECT_OS_WINDOWS
#  include <windows.h>
#else
#  error Unsupported OS
#endif


int64_t
os_time_get_nano(void)
{
#if DETECT_OS_LINUX || DETECT_OS_BSD

   struct timespec tv;
   clock_gettime(CLOCK_MONOTONIC, &tv);
   return tv.tv_nsec + tv.tv_sec*INT64_C(1000000000);

#elif DETECT_OS_UNIX

   struct timeval tv;
   gettimeofday(&tv, NULL);
   return tv.tv_usec*INT64_C(1000) + tv.tv_sec*INT64_C(1000000000);

#elif DETECT_OS_WINDOWS

   static LARGE_INTEGER frequency;
   LARGE_INTEGER counter;
   int64_t secs, nanosecs;
   if(!frequency.QuadPart)
      QueryPerformanceFrequency(&frequency);
   QueryPerformanceCounter(&counter);
   /* Compute seconds and nanoseconds parts separately to
    * reduce severity of precision loss.
    */
   secs = counter.QuadPart / frequency.QuadPart;
   nanosecs = (counter.QuadPart % frequency.QuadPart) * INT64_C(1000000000)
      / frequency.QuadPart;
   return secs*INT64_C(1000000000) + nanosecs;

#else

#error Unsupported OS

#endif
}



void
os_time_sleep(int64_t usecs)
{
#if DETECT_OS_LINUX
   struct timespec time;
   time.tv_sec = usecs / 1000000;
   time.tv_nsec = (usecs % 1000000) * 1000;
   while (clock_nanosleep(CLOCK_MONOTONIC, 0, &time, &time) == EINTR);

#elif DETECT_OS_UNIX
   usleep(usecs);

#elif DETECT_OS_WINDOWS
   DWORD dwMilliseconds = (DWORD) ((usecs + 999) / 1000);
   /* Avoid Sleep(O) as that would cause to sleep for an undetermined duration */
   if (dwMilliseconds) {
      Sleep(dwMilliseconds);
   }
#else
#  error Unsupported OS
#endif
}



int64_t
os_time_get_absolute_timeout(uint64_t timeout)
{
   int64_t time, abs_timeout;

   /* Also check for the type upper bound. */
   if (timeout == OS_TIMEOUT_INFINITE || timeout > INT64_MAX)
      return OS_TIMEOUT_INFINITE;

   time = os_time_get_nano();
   abs_timeout = time + (int64_t)timeout;

   /* Check for overflow. */
   if (abs_timeout < time)
      return OS_TIMEOUT_INFINITE;

   return abs_timeout;
}


bool
os_wait_until_zero(volatile int *var, uint64_t timeout)
{
   if (!p_atomic_read(var))
      return true;

   if (!timeout)
      return false;

   if (timeout == OS_TIMEOUT_INFINITE) {
      while (p_atomic_read(var)) {
#if DETECT_OS_UNIX
         sched_yield();
#endif
      }
      return true;
   }
   else {
      int64_t start_time = os_time_get_nano();
      int64_t end_time = start_time + timeout;

      while (p_atomic_read(var)) {
         if (os_time_timeout(start_time, end_time, os_time_get_nano()))
            return false;

#if DETECT_OS_UNIX
         sched_yield();
#endif
      }
      return true;
   }
}


bool
os_wait_until_zero_abs_timeout(volatile int *var, int64_t timeout)
{
   if (!p_atomic_read(var))
      return true;

   if (timeout == OS_TIMEOUT_INFINITE)
      return os_wait_until_zero(var, OS_TIMEOUT_INFINITE);

   while (p_atomic_read(var)) {
      if (os_time_get_nano() >= timeout)
         return false;

#if DETECT_OS_UNIX
      sched_yield();
#endif
   }
   return true;
}
