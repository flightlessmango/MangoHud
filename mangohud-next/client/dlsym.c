#include <dlfcn.h>
#include <string.h>
#include "real_dlsym.h"

// C avoids the implicit C++ noexcept cleanup that Clang cannot skip for musttail.
MANGOHUD_DLSYM_ATTRIBUTES void* dlsym(void* handle, const char* symbol)
{
    void* hook = (handle == RTLD_DEFAULT || handle == RTLD_NEXT ||
                  strncmp(symbol, "wl_", 3) != 0)
        ? mangohud_find_dlsym_ptr(symbol) : NULL;
    if (hook)
        return hook;

    MANGOHUD_MUSTTAIL return get_next_dlsym()(handle, symbol);
}
