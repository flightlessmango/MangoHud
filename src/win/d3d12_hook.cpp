#include "kiero.h"
#include "d3d12_hook.h"
#include <cstdio>
#include <cassert>
#include <functional>

typedef long(__fastcall* PresentD3D12) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
PresentD3D12 oPresentD3D12;

long __fastcall hkPresent12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags){
    printf("d3d12 present\n");
    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
}

void impl::d3d12::init()
{
    auto ret = kiero::bind(140, (void**)&oPresentD3D12, reinterpret_cast<void*>(hkPresent12));
    assert(ret == kiero::Status::Success);
}