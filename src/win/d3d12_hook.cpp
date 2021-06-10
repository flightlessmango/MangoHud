#include <cstdio>
#include <cassert>
#include "kiero.h"
#include "d3d12_hook.h"
#include "d3d_shared.h"
#include "../overlay.h"

typedef long(__fastcall* PresentD3D12) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
PresentD3D12 oPresentD3D12;

long __fastcall hkPresent12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags){
    dx_version = kiero::RenderType::D3D12;
    d3d_run();
    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
}

void impl::d3d12::init()
{
    printf("init d3d12\n");
    auto ret = kiero::bind(140, (void**)&oPresentD3D12, reinterpret_cast<void*>(hkPresent12));
	if(ret != kiero::Status::Success)
        printf("not dx12\n");
    else
        init_d3d_shared();
}