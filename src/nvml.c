#include "nvidia_info.h"
#include <nvml.h>

void checkNvidia(){
    result = nvmlInit();
    if (NVML_SUCCESS != result) {
        
    } else {
        nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);
        nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
        nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    }
} 
