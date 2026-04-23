#pragma once

#include <string>
#include <memory>
#include <atomic>

#define NVAPI_MAX_PHYSICAL_GPUS 64
#define NVAPI_INIT_ID 0x0150e828
#define NVAPI_GET_ERROR_MESSAGE 0x6c2d048c
#define NVAPI_ENUM_PHYSICAL_GPUS 0xe5ac921f
#define NVAPI_GET_BUS_ID 0x1be0b8e5
#define NVAPI_GET_CURRENT_VOLTAGE 0x465f9bcf
#define NVAPI_GET_FULL_NAME 0xceee8e9f
#define NVAPI_GET_THERMAL_SENSORS 0x65fe3aad

class libnvapi_loader {
public:
    libnvapi_loader();
    ~libnvapi_loader();

    struct NvApiVoltage {
        unsigned int version = sizeof(NvApiVoltage) | (1 << 16);
        unsigned int flags;
        unsigned int padding_1[8];
        unsigned int value_microvolts;
        unsigned int padding_2[8];
    };

    struct NvThermalSensors {
        unsigned int version = sizeof(NvThermalSensors) | (2 << 16);
        unsigned int mask;
        int reserved[8];
        int temperatures[32];
    };

    bool load();
    bool is_loaded() { return loaded_; }

    void* (*nvapi_QueryInterface)(unsigned int);
    int (*nvapi_Initialize)();
    int (*nvapi_GetErrorMessage)(int, char(*)[64]);
    int (*nvapi_EnumPhysicalGPUs)(void*, unsigned int*);
    int (*nvapi_GPU_GetBusId)(long, unsigned int*);
    int (*nvapi_GetVoltage)(long, NvApiVoltage*);
    int (*nvapi_GPU_GetFullName)(long, char*);
    int (*nvapi_GPU_GetThermalSensors)(long, NvThermalSensors*);

private:
    void unload();
    void* library_ = nullptr;

    const std::string library_name = "libnvidia-api.so.1";
    std::atomic<bool> loaded_ = false;

    // Disallow copy constructor and assignment operator.
    libnvapi_loader(const libnvapi_loader&);
    void operator=(const libnvapi_loader&);
};

std::shared_ptr<libnvapi_loader> get_libnvapi_loader();
