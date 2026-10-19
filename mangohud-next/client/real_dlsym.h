#pragma once
#ifndef MANGOHUD_REAL_DLSYM_H
#define MANGOHUD_REAL_DLSYM_H

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type

// Forwarding dlsym must preserve its caller for RTLD_NEXT lookups.
#if defined(__has_attribute)
# if __has_attribute(musttail)
#  define MANGOHUD_MUSTTAIL __attribute__((musttail))
# endif
#endif
#ifdef MANGOHUD_MUSTTAIL
# define MANGOHUD_DLSYM_ATTRIBUTES
#else
# define MANGOHUD_MUSTTAIL
// Older GCC needs sibling-call optimization even in debug builds.
# define MANGOHUD_DLSYM_ATTRIBUTES __attribute__((optimize("O2", "optimize-sibling-calls")))
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef void *(*dlsym_func)(void *, const char *);
void *real_dlopen(const char *filename, int flag);
void* real_dlsym( void*, const char* );
dlsym_func get_next_dlsym(void);
void* mangohud_find_dlsym_ptr(const char* name);
void* get_proc_address(const char* name);
char* real_dlerror(void);
#ifdef __cplusplus
}
#endif

#endif //MANGOHUD_REAL_DLSYM_H
