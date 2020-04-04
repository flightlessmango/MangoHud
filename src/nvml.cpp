#include "loaders/loader_nvml.h"
#include "nvidia_info.h"
#include <iostream>

#ifdef __gnu_linux__
libnvml_loader nvml("libnvidia-ml.so.1");
#endif
#ifdef _WIN32
libnvml_loader nvml(NVMLQUERY_DEFAULT_NVML_DLL_PATH);
#endif

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
bool nvmlSuccess = false;
unsigned int nvidiaTemp, nvidiaCoreClock, nvidiaMemClock;
struct nvmlUtilization_st nvidiaUtilization;
struct nvmlMemory_st nvidiaMemory {};

bool checkNVML(const char* pciBusId){
    if (nvml.IsLoaded()){
        result = nvml.nvmlInit();
        if (NVML_SUCCESS != result) {
            std::cerr << "MANGOHUD: Nvidia module not loaded\n";
        } else {
            nvmlReturn_t ret = NVML_ERROR_UNKNOWN;
            if (pciBusId && ((ret = nvml.nvmlDeviceGetHandleByPciBusId(pciBusId, &nvidiaDevice)) != NVML_SUCCESS)) {
                std::cerr << "MANGOHUD: Getting device handle by PCI bus ID failed: " << nvml.nvmlErrorString(ret) << "\n";
                std::cerr << "          Using index 0.\n";
            }

            if (ret != NVML_SUCCESS)
                ret = nvml.nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);

            if (ret != NVML_SUCCESS)
                std::cerr << "MANGOHUD: Getting device handle failed: " << nvml.nvmlErrorString(ret) << "\n";

            nvmlSuccess = (ret == NVML_SUCCESS);
            return nvmlSuccess;
        }
    } else {
        std::cerr << "MANGOHUD: Failed to load NVML\n";
    }
    return false;
} 

bool getNVMLInfo(){
    nvmlReturn_t response;
    response = nvml.nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(nvidiaDevice, &nvidiaMemory);
    nvml.nvmlDeviceGetClockInfo(nvidiaDevice, NVML_CLOCK_GRAPHICS, &nvidiaCoreClock);
    nvml.nvmlDeviceGetClockInfo(nvidiaDevice, NVML_CLOCK_MEM, &nvidiaMemClock);
    if (response == NVML_ERROR_NOT_SUPPORTED)
        nvmlSuccess = false;
    return nvmlSuccess;
}