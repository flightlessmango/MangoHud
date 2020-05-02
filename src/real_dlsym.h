#pragma once

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type

void *real_dlopen(const char *filename, int flag);
void* real_dlsym( void*, const char* );
void* get_proc_address(const char* name);
