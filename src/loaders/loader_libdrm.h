
#ifndef LIBRARY_LOADER_LIBDRM_H
#define LIBRARY_LOADER_LIBDRM_H

#define LIBRARY_LOADER_LIBDRM_H_DLOPEN

#include <dlfcn.h>
#include <xf86drm.h>
//#include <libdrm/amdgpu_drm.h>
//#include <libdrm/amdgpu.h>

typedef struct amdgpu_device *amdgpu_device_handle;
int amdgpu_device_initialize(int fd,
		uint32_t *major_version,
		uint32_t *minor_version,
		amdgpu_device_handle *device_handle);
int amdgpu_device_deinitialize(amdgpu_device_handle device_handle);
int amdgpu_query_info(amdgpu_device_handle dev, unsigned info_id,
		unsigned size, void *value);
int amdgpu_query_sensor_info(amdgpu_device_handle dev, unsigned sensor_type,
		unsigned size, void *value);
int amdgpu_read_mm_registers(amdgpu_device_handle dev, unsigned dword_offset,
		unsigned count, uint32_t instance, uint32_t flags,
		uint32_t *values);

class libdrm_loader {
 public:
  libdrm_loader();
  ~libdrm_loader();

  bool Load();
  bool IsLoaded() { return loaded_; }

  decltype(&::drmGetVersion) drmGetVersion;
  decltype(&::drmFreeVersion) drmFreeVersion;
  decltype(&::drmCommandWriteRead) drmCommandWriteRead;

 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN)
  void* library;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libdrm_loader(const libdrm_loader&);
  void operator=(const libdrm_loader&);
};

class libdrm_amdgpu_loader {
 public:
  libdrm_amdgpu_loader();
  ~libdrm_amdgpu_loader();

  bool Load();
  bool IsLoaded() { return loaded_; }

  decltype(&::amdgpu_device_initialize) amdgpu_device_initialize;
  decltype(&::amdgpu_device_deinitialize) amdgpu_device_deinitialize;
  decltype(&::amdgpu_query_info) amdgpu_query_info;
  decltype(&::amdgpu_query_sensor_info) amdgpu_query_sensor_info;
  decltype(&::amdgpu_read_mm_registers) amdgpu_read_mm_registers;

 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_LIBDRM_H_DLOPEN)
  void* library;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libdrm_amdgpu_loader(const libdrm_amdgpu_loader&);
  void operator=(const libdrm_amdgpu_loader&);
};

extern libdrm_loader g_libdrm;

#endif  // LIBRARY_LOADER_LIBDRM_H
