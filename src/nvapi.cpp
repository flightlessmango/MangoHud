#include <windows.h>
#include <iostream>
#include "nvidia_info.h"
#include "gpu.h"

// magic numbers, do not change them
#define NVAPI_MAX_PHYSICAL_GPUS   64
#define NVAPI_MAX_USAGES_PER_GPU  34

// function pointer types
typedef int *(*NvAPI_QueryInterface_t)(unsigned int offset);
typedef int (*NvAPI_Initialize_t)();
typedef int (*NvAPI_EnumPhysicalGPUs_t)(int **handles, int *count);
typedef int (*NvAPI_GPU_GetUsages_t)(int *handle, unsigned int *usages);

NvAPI_QueryInterface_t      NvAPI_QueryInterface     = NULL;
NvAPI_Initialize_t          NvAPI_Initialize         = NULL;
NvAPI_EnumPhysicalGPUs_t    NvAPI_EnumPhysicalGPUs   = NULL;
NvAPI_GPU_GetUsages_t       NvAPI_GPU_GetUsages      = NULL;
HMODULE hmod;
bool init_nvapi_bool;
int         *gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = { NULL };
int          gpuCount = 0;
unsigned int gpuUsages[NVAPI_MAX_USAGES_PER_GPU] = { 0 };

bool checkNVAPI(){

#if _WIN64
    hmod = LoadLibraryA("nvapi64.dll");
#else
    hmod = LoadLibraryA("nvapi.dll");
#endif

    if (hmod == NULL)
    {
        printf("Failed to load nvapi.dll");
        return false;
    }
    NvAPI_QueryInterface = (NvAPI_QueryInterface_t) GetProcAddress(hmod, "nvapi_QueryInterface");
    NvAPI_Initialize = (NvAPI_Initialize_t) (*NvAPI_QueryInterface)(0x0150E828);
    NvAPI_EnumPhysicalGPUs = (NvAPI_EnumPhysicalGPUs_t) (*NvAPI_QueryInterface)(0xE5AC921F);
    NvAPI_GPU_GetUsages = (NvAPI_GPU_GetUsages_t) (*NvAPI_QueryInterface)(0x189A1FDF);

    if (!NvAPI_Initialize || !NvAPI_EnumPhysicalGPUs || !NvAPI_EnumPhysicalGPUs || !NvAPI_GPU_GetUsages)
    {
        std::cerr << "Couldn't get functions in nvapi.dll" << std::endl;
        return 2;
    }
    NvAPI_Initialize();

    NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);

    return true;
}

bool NVAPIInfo::init()
{
    if (!init_nvapi_bool)
        init_nvapi_bool = checkNVAPI();
    return init_nvapi_bool;
}

void NVAPIInfo::update()
{
    if (!init_nvapi_bool){
        init_nvapi_bool = checkNVAPI();
    }

    gpuUsages[0] = (NVAPI_MAX_USAGES_PER_GPU * 4) | 0x10000;
    NvAPI_GPU_GetUsages(gpuHandles[0], gpuUsages);
    if (g_active_gpu)
        g_active_gpu->s.load = gpuUsages[3];
}
