#include "nvapi_loader.hpp"
#include <dlfcn.h>
#include <spdlog/spdlog.h>

static std::shared_ptr<libnvapi_loader> libnvapi_;

std::shared_ptr<libnvapi_loader> get_libnvapi_loader()
{
    if (!libnvapi_)
        libnvapi_ = std::make_shared<libnvapi_loader>();

    return libnvapi_;
}

libnvapi_loader::libnvapi_loader() {
    load();
}

libnvapi_loader::~libnvapi_loader() {
    unload();
}

#define LOAD_NVAPI_FUNCTION(name, id) \
    do {                    \
        name = reinterpret_cast<decltype(this->name)>(nvapi_QueryInterface(id)); \
        if (!name) { \
            SPDLOG_ERROR("Failed to find address of {}", #name); \
            unload(); \
            return false; \
        } \
    } while(0)

bool libnvapi_loader::load() {
    if (loaded_)
        return true;

    library_ = dlopen(library_name.c_str(), RTLD_LAZY | RTLD_NODELETE);

    if (!library_) {
        SPDLOG_ERROR("Failed to open {}: {}", library_name, dlerror());
        return false;
    }

    nvapi_QueryInterface =
        reinterpret_cast<decltype(nvapi_QueryInterface)>(dlsym(library_, "nvapi_QueryInterface"));

    if (!nvapi_QueryInterface) {
        SPDLOG_ERROR("Failed to find address of nvapi_QueryInterface");
        return false;
    }

    LOAD_NVAPI_FUNCTION(nvapi_Initialize, NVAPI_INIT_ID);
    LOAD_NVAPI_FUNCTION(nvapi_GetErrorMessage, NVAPI_GET_ERROR_MESSAGE);
    LOAD_NVAPI_FUNCTION(nvapi_EnumPhysicalGPUs, NVAPI_ENUM_PHYSICAL_GPUS);
    LOAD_NVAPI_FUNCTION(nvapi_GPU_GetBusId, NVAPI_GET_BUS_ID);
    LOAD_NVAPI_FUNCTION(nvapi_GetVoltage, NVAPI_GET_CURRENT_VOLTAGE);
    LOAD_NVAPI_FUNCTION(nvapi_GPU_GetFullName, NVAPI_GET_FULL_NAME);
    LOAD_NVAPI_FUNCTION(nvapi_GPU_GetThermalSensors, NVAPI_GET_THERMAL_SENSORS);

    loaded_ = true;
    return true;
}

#undef LOAD_NVAPI_FUNCTION

void libnvapi_loader::unload() {
    if (library_) {
        dlclose(library_);
        library_ = NULL;
    }

    loaded_ = false;

    nvapi_QueryInterface = nullptr;
    nvapi_Initialize = nullptr;
    nvapi_GetErrorMessage = nullptr;
    nvapi_EnumPhysicalGPUs = nullptr;
    nvapi_GPU_GetBusId = nullptr;
    nvapi_GetVoltage = nullptr;
}
