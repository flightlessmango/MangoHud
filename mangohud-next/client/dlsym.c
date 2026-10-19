#include <dlfcn.h>
#include "real_dlsym.h"

// C avoids the implicit C++ noexcept cleanup that Clang cannot skip for musttail.
MANGOHUD_DLSYM_ATTRIBUTES void* dlsym(void* handle, const char* symbol)
{
    void* hook = mangohud_find_dlsym_ptr(symbol);
    if (hook)
        return hook;

    MANGOHUD_MUSTTAIL return get_next_dlsym()(handle, symbol);
}
