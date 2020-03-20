#include "loaders/loader_nvml.h"
#include "nvidia_info.h"

libnvml_loader nvml("libnvidia-ml.so.1");

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
bool nvmlSuccess = false;
unsigned int nvidiaTemp, nvidiaCoreClock, nvidiaMemClock;
struct nvmlUtilization_st nvidiaUtilization;
struct nvmlMemory_st nvidiaMemory {};

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
    nvmlReturn_t response;
    nvml.nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);
    response = nvml.nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(nvidiaDevice, &nvidiaMemory);
    nvml.nvmlDeviceGetClockInfo(nvidiaDevice, NVML_CLOCK_GRAPHICS, &nvidiaCoreClock);
    nvml.nvmlDeviceGetClockInfo(nvidiaDevice, NVML_CLOCK_MEM, &nvidiaMemClock);
    if (response == 3)
        nvmlSuccess = false;
}