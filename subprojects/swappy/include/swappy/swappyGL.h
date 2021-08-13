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

/**
 * @defgroup swappyGL Swappy for OpenGL
 * OpenGL part of Swappy.
 * @{
 */

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#include <stdint.h>

#include "swappy_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Swappy, getting the required Android parameters from the
 * display subsystem via JNI.
 * @param env The JNI environment where Swappy is used
 * @param jactivity The activity where Swappy is used
 * @return false if Swappy failed to initialize.
 * @see SwappyGL_destroy
 */
bool SwappyGL_init(JNIEnv *env, jobject jactivity);

/**
 * @brief Check if Swappy was successfully initialized.
 * @return false if either the `swappy.disable` system property is not `false`
 * or the required OpenGL extensions are not available for Swappy to work.
 */
bool SwappyGL_isEnabled();

/**
 * @brief Destroy resources and stop all threads that Swappy has created.
 * @see SwappyGL_init
 */
void SwappyGL_destroy();

/**
 * @brief Tell Swappy which ANativeWindow to use when calling to ANativeWindow_*
 * API.
 * @param window ANativeWindow that was used to create the EGLSurface.
 * @return true on success, false if Swappy was not initialized.
 */
bool SwappyGL_setWindow(ANativeWindow *window);

/**
 * @brief Replace calls to eglSwapBuffers with this. Swappy will wait for the
 * previous frame's buffer to be processed by the GPU before actually calling
 * eglSwapBuffers.
 * @return true on success or false if
 * 1) Swappy is not initialized or 2) eglSwapBuffers did not return EGL_TRUE.
 * In the latter case, eglGetError can be used to get the error code.
 */
bool SwappyGL_swap(EGLDisplay display, EGLSurface surface);

// Paramter setters:

void SwappyGL_setUseAffinity(bool tf);

/**
 * @brief Override the swap interval
 *
 * By default, Swappy will adjust the swap interval based on actual frame
 * rendering time.
 *
 * If an app wants to override the swap interval calculated by Swappy, it can
 * call this function:
 *
 * * This will temporarily override Swappy's frame timings but, unless
 *   `SwappyGL_setAutoSwapInterval(false)` is called, the timings will continue
 * to be be updated dynamically, so the swap interval may change.
 *
 * * This set the **minimal** interval to run. For example,
 * `SwappyGL_setSwapIntervalNS(SWAPPY_SWAP_30FPS)` will not allow Swappy to swap
 * faster, even if auto mode decides that it can. But it can go slower if auto
 * mode is on.
 *
 * @param swap_ns The new swap interval value, in nanoseconds.
 */
void SwappyGL_setSwapIntervalNS(uint64_t swap_ns);

/**
 * @brief Set the fence timeout parameter, for devices with faulty
 * drivers. Its default value is 50,000,000ns (50ms).
 */
void SwappyGL_setFenceTimeoutNS(uint64_t fence_timeout_ns);

// Parameter getters:

/**
 * @brief Get the refresh period value, in nanoseconds.
 */
uint64_t SwappyGL_getRefreshPeriodNanos();

/**
 * @brief Get the swap interval value, in nanoseconds.
 */
uint64_t SwappyGL_getSwapIntervalNS();

bool SwappyGL_getUseAffinity();

/**
 * @brief Get the fence timeout value, in nanoseconds.
 */
uint64_t SwappyGL_getFenceTimeoutNS();

/**
 * @brief Set the number of bad frames to wait before applying a fix for buffer
 * stuffing. Set to zero in order to turn off this feature. Default value = 0.
 */
void SwappyGL_setBufferStuffingFixWait(int32_t n_frames);

#ifdef __cplusplus
};
#endif

/** @} */
