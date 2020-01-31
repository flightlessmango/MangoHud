#include "nvidia_info.h"
#include <nvml.h>

void checkNvidia(){
    result = nvmlInit();
    if (NVML_SUCCESS != result) {
        printf("MANGOHUD: Nvidia module not loaded\n");
    } else {
        nvmlSuccess = true;
    }
} 

void getNvidiaInfo(){
    nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);
    nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
    nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
}