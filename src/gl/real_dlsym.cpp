#include "real_dlsym.h"

#include <stdlib.h>
#include <dlfcn.h>

extern "C" void* __libc_dlsym( void* handle, const char* name );

void* real_dlsym( void* handle, const char* name )
{
    static void *(*the_real_dlsym)( void*, const char* );

    if (!the_real_dlsym) {
        void* libdl = dlopen( "libdl.so", RTLD_NOW | RTLD_LOCAL );
        the_real_dlsym = reinterpret_cast<decltype(the_real_dlsym)> (__libc_dlsym( libdl, "dlsym" ));
    }

    return the_real_dlsym( handle, name );
}
