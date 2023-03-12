#include "kiero.h"
#include "windows.h"
#include <dxgi.h>
#include <cstdio>
#include "dxgi.h"

#ifdef _UNICODE
# define KIERO_TEXT(text) L##text
#else
# define KIERO_TEXT(text) text
#endif

uint32_t get_device_id_dxgi(){
    HMODULE libDXGI;
    if ((libDXGI = ::GetModuleHandle(KIERO_TEXT("dxgi.dll"))) == NULL){
        printf("dxgi not found\n");
        return 0;
    }
    auto CreateDXGIFactory = reinterpret_cast<decltype(&::CreateDXGIFactory)>(::GetProcAddress(libDXGI, "CreateDXGIFactory"));
    if (!CreateDXGIFactory)
    {   
        printf("can't create dxgi factory\n");
        return 0;
    }
    IDXGIAdapter* dxgi_adapter;
    IDXGIFactory* dxgi_factory;
    if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&dxgi_factory) < 0)
    {
        printf("can't assign factory\n");
        return 0;
    }
    DXGI_ADAPTER_DESC AdapterDesc;
    int i;
    for (i = 0; SUCCEEDED(dxgi_factory->EnumAdapters(i, &dxgi_adapter)); i++) {
        dxgi_adapter->GetDesc(&AdapterDesc);
        if (AdapterDesc.VendorId == 0x10de)
            return AdapterDesc.VendorId;
        if (AdapterDesc.VendorId == 0x1002)
            return AdapterDesc.VendorId;
        if (AdapterDesc.VendorId == 0x8086)
            return AdapterDesc.VendorId;
    }
    return 0;
}