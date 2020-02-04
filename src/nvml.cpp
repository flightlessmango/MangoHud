#include "loaders/loader_nvml.h"
#include "nvidia_info.h"

libnvml_loader nvml("libnvidia-ml.so.1");

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
bool nvmlSuccess = false;
unsigned int nvidiaTemp;
struct nvmlUtilization_st nvidiaUtilization;
struct nvmlMemory_st nvidiaMemory;

bool checkNvidia(){
    if (nvml.IsLoaded()){
        result = nvml.nvmlInit();
        if (NVML_SUCCESS != result) {
            printf("MANGOHUD: Nvidia module not loaded\n");
        } else {
            nvmlSuccess = true;
            return true;
        }
    } else {
        printf("Failed to load NVML!\n");
    }
    return false;
} 

void getNvidiaInfo(){
    nvml.nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);
    nvml.nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(nvidiaDevice, &nvidiaMemory);
}