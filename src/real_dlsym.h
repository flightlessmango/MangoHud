#pragma once
#ifndef MANGOHUD_REAL_DLSYM_H
#define MANGOHUD_REAL_DLSYM_H

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type

#ifdef __cplusplus
extern "C" {
#endif
void *real_dlopen(const char *filename, int flag);
void* real_dlsym( void*, const char* );
void* get_proc_address(const char* name);
char* real_dlerror(void);
#ifdef __cplusplus
}
#endif

#endif //MANGOHUD_REAL_DLSYM_H
