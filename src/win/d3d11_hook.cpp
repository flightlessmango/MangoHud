#include "kiero.h"

#if KIERO_INCLUDE_D3D11

#include "d3d11_hook.h"
#include <d3d11.h>
#include <assert.h>

#include "d3d_shared.h"

typedef long(__stdcall* Present)(IDXGISwapChain*, UINT, UINT);
static Present oPresent = NULL;

long __stdcall hkPresent11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    d3d_run();
	return oPresent(pSwapChain, SyncInterval, Flags);
}

void impl::d3d11::init()
{
    printf("init d3d11\n");
	auto ret = kiero::bind(8, (void**)&oPresent, reinterpret_cast<void *>(hkPresent11));
	assert(ret == kiero::Status::Success);
    init_d3d_shared();
}

#endif // KIERO_INCLUDE_D3D11