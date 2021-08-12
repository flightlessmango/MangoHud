
#include "loaders/loader_libdrm.h"
#include <iostream>
#include <spdlog/spdlog.h>

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN) && !defined(LIBRARY_LOADER_LIBDRM_H_DT_NEEDED)
#error neither LIBRARY_LOADER_LIBDRM_H_DLOPEN nor LIBRARY_LOADER_LIBDRM_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN) && defined(LIBRARY_LOADER_LIBDRM_H_DT_NEEDED)
#error both LIBRARY_LOADER_LIBDRM_H_DLOPEN and LIBRARY_LOADER_LIBDRM_H_DT_NEEDED defined
#endif

libdrm_loader::libdrm_loader() : loaded_(false) {
  Load();
}

libdrm_loader::~libdrm_loader() {
  CleanUp(loaded_);
}

bool libdrm_loader::Load() {
  if (loaded_) {
    return true;
  }

#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN)
  library = dlopen("libdrm.so.2", RTLD_LAZY);
  if (!library) {
    SPDLOG_ERROR("Failed to open " MANGOHUD_ARCH " libdrm.so.2: {}", dlerror());
    return false;
  }

  drmGetVersion =
      reinterpret_cast<decltype(this->drmGetVersion)>(
          dlsym(library, "drmGetVersion"));
  if (!drmGetVersion) {
    CleanUp(true);
    return false;
  }

  drmFreeVersion =
      reinterpret_cast<decltype(this->drmFreeVersion)>(
          dlsym(library, "drmFreeVersion"));
  if (!drmFreeVersion) {
    CleanUp(true);
    return false;
  }

  drmCommandWriteRead =
      reinterpret_cast<decltype(this->drmCommandWriteRead)>(
          dlsym(library, "drmCommandWriteRead"));
  if (!drmCommandWriteRead) {
    CleanUp(true);
    return false;
  }

#endif

#if defined(LIBRARY_LOADER_LIBDRM_H_DT_NEEDED)
  drmGetVersion = &::drmGetVersion;
  drmFreeVersion = &::drmFreeVersion;
  drmCommandWriteRead = &::drmCommandWriteRead;
#endif

  loaded_ = true;
  return true;
}

void libdrm_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN)
  if (unload) {
    dlclose(library);
    library = nullptr;
  }
#endif
  loaded_ = false;
  drmGetVersion = nullptr;
  drmFreeVersion = nullptr;
  drmCommandWriteRead = nullptr;

}

libdrm_amdgpu_loader::libdrm_amdgpu_loader() : loaded_(false) {
  Load();
}

libdrm_amdgpu_loader::~libdrm_amdgpu_loader() {
  CleanUp(loaded_);
}

bool libdrm_amdgpu_loader::Load() {
  if (loaded_) {
    return true;
  }

#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN)
  library = dlopen("libdrm_amdgpu.so.1", RTLD_LAZY);
  if (!library) {
    SPDLOG_ERROR("Failed to open " MANGOHUD_ARCH " libdrm_amdgpu.so.1: {}", dlerror());
    CleanUp(true);
    return false;
  }

  amdgpu_device_initialize =
      reinterpret_cast<decltype(this->amdgpu_device_initialize)>(
          dlsym(library, "amdgpu_device_initialize"));
  if (!amdgpu_device_initialize) {
    CleanUp(true);
    return false;
  }

  amdgpu_device_deinitialize =
      reinterpret_cast<decltype(this->amdgpu_device_deinitialize)>(
          dlsym(library, "amdgpu_device_deinitialize"));
  if (!amdgpu_device_deinitialize) {
    CleanUp(true);
    return false;
  }

  amdgpu_query_info =
      reinterpret_cast<decltype(this->amdgpu_query_info)>(
          dlsym(library, "amdgpu_query_info"));
  if (!amdgpu_query_info) {
    CleanUp(true);
    return false;
  }

  amdgpu_query_sensor_info =
      reinterpret_cast<decltype(this->amdgpu_query_sensor_info)>(
          dlsym(library, "amdgpu_query_sensor_info"));
  if (!amdgpu_query_sensor_info) {
    CleanUp(true);
    return false;
  }

  amdgpu_read_mm_registers =
      reinterpret_cast<decltype(this->amdgpu_read_mm_registers)>(
          dlsym(library, "amdgpu_read_mm_registers"));
  if (!amdgpu_read_mm_registers) {
    CleanUp(true);
    return false;
  }

#endif

#if defined(LIBRARY_LOADER_LIBDRM_H_DT_NEEDED)
  amdgpu_device_initialize = &::amdgpu_device_initialize;
  amdgpu_device_deinitialize = &::amdgpu_device_deinitialize;
  amdgpu_query_info = &::amdgpu_query_info;
  amdgpu_query_sensor_info = &::amdgpu_query_sensor_info;
  amdgpu_read_mm_registers = &::amdgpu_read_mm_registers;

#endif

  loaded_ = true;
  return true;
}

void libdrm_amdgpu_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN)
  if (unload) {
    dlclose(library);
    library = nullptr;
  }
#endif
  loaded_ = false;
  amdgpu_device_initialize = nullptr;
  amdgpu_device_deinitialize = nullptr;
  amdgpu_query_info = nullptr;
  amdgpu_query_sensor_info = nullptr;
  amdgpu_read_mm_registers = nullptr;

}

libdrm_loader g_libdrm;
