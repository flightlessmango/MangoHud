#include "loaders/loader_nvml.h"
#include "nvidia_info.h"

libnvml_loader nvml("libnvidia-ml.so.1");

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
static bool nvmlSuccess = false;
static bool nvmlAlreadyChecked = false;
unsigned int nvidiaTemp = 0;
struct nvmlUtilization_st nvidiaUtilization {};
struct nvmlMemory_st nvidiaMemory {};

bool checkNvidia(){
    if (nvmlAlreadyChecked)
        return nvmlSuccess;

    if (nvml.IsLoaded()){
        result = nvml.nvmlInit();
        if (NVML_SUCCESS != result) {
            printf("MANGOHUD: Nvidia module not loaded\n");
        } else {
            nvmlSuccess = true;
        }
    } else {
        printf("Failed to load NVML!\n");
    }

    nvmlAlreadyChecked = true;
    return nvmlSuccess;
} 

bool getNvidiaInfo(int& gpuLoad, int& gpuTemp, float& gpuMemUsed){
    nvml.nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);
    nvml.nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(nvidiaDevice, &nvidiaMemory);
    gpuLoad = nvidiaUtilization.gpu;
    gpuTemp = nvidiaTemp;
    gpuMemUsed = nvidiaMemory.used / (1024.f * 1024.f);
    return true;
}