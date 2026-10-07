#include "nvapi_sensors.h"
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace {

const char* NVAPI_LIBRARY = "libnvidia-api.so.1";

// NVAPI has no exported symbols beyond nvapi_QueryInterface; every entry point
// is looked up by a numeric id.
const uint32_t QUERY_INITIALIZE = 0x0150e828;
const uint32_t QUERY_UNLOAD = 0xd22bdd7e;
const uint32_t QUERY_ENUM_PHYSICAL_GPUS = 0xe5ac921f;
const uint32_t QUERY_GET_BUS_ID = 0x1be0b8e5;
const uint32_t QUERY_THERMALS = 0x65fe3aad;
const uint32_t QUERY_VOLTAGE = 0x465f9bcf;

const int MAX_PHYSICAL_GPUS = 64;

// Thermal values are fixed point with 8 fractional bits.
const int FIXED_POINT_SCALE = 256;

const int MICROVOLTS_PER_MILLIVOLT = 1000;
const int MAX_PLAUSIBLE_MILLIVOLTS = 2000;

} // namespace

NvApiSensors::~NvApiSensors() {
    if (library_) {
        if (query_interface_) {
            typedef int32_t (*unload_fn)(void);
            unload_fn unload = (unload_fn)query_interface_(QUERY_UNLOAD);
            if (unload)
                unload();
        }
        dlclose(library_);
    }
}

bool NvApiSensors::find_gpu(unsigned bus) {
    typedef int32_t (*enum_fn)(void**, uint32_t*);
    typedef int32_t (*bus_fn)(void*, uint32_t*);

    enum_fn enum_gpus = (enum_fn)query_interface_(QUERY_ENUM_PHYSICAL_GPUS);
    bus_fn get_bus = (bus_fn)query_interface_(QUERY_GET_BUS_ID);
    if (!enum_gpus || !get_bus)
        return false;

    void* handles[MAX_PHYSICAL_GPUS];
    memset(handles, 0, sizeof(handles));
    uint32_t count = 0;

    if (enum_gpus(handles, &count) != 0)
        return false;

    for (uint32_t i = 0; i < count && i < MAX_PHYSICAL_GPUS; i++) {
        uint32_t id = 0;
        if (get_bus(handles[i], &id) == 0 && id == bus) {
            gpu_ = handles[i];
            return true;
        }
    }

    return false;
}

bool NvApiSensors::calculate_mask() {
    // The sensor mask this GPU accepts is not reported anywhere, so widen it one
    // bit at a time until the call is rejected and keep the last accepted value.
    for (int bit = 0; bit < 32; bit++) {
        Thermals probe;
        memset(&probe, 0, sizeof(probe));
        probe.version = (uint32_t)(sizeof(Thermals) | (2 << 16));
        const uint32_t probe_mask = 1u << bit;
        probe.mask = (int32_t)probe_mask;

        if (get_thermals_(gpu_, &probe) != 0) {
            mask_ = (int32_t)(probe_mask - 1u);
            return mask_ != 0;
        }
    }

    return false;
}

bool NvApiSensors::init(const char* pci_bus_id) {
    unsigned domain, bus, device, function;

    if (!pci_bus_id ||
        sscanf(pci_bus_id, "%x:%x:%x.%x", &domain, &bus, &device, &function) != 4) {
        SPDLOG_DEBUG("NvAPI: could not parse PCI bus id");
        return false;
    }

    library_ = dlopen(NVAPI_LIBRARY, RTLD_NOW);
    if (!library_) {
        SPDLOG_DEBUG("NvAPI: {} not available, extra sensors disabled", NVAPI_LIBRARY);
        return false;
    }

    query_interface_ = (void* (*)(uint32_t))dlsym(library_, "nvapi_QueryInterface");
    if (!query_interface_) {
        SPDLOG_DEBUG("NvAPI: nvapi_QueryInterface missing");
        return false;
    }

    typedef int32_t (*init_fn)(void);
    init_fn initialize = (init_fn)query_interface_(QUERY_INITIALIZE);
    if (!initialize || initialize() != 0) {
        SPDLOG_DEBUG("NvAPI: initialization failed");
        return false;
    }

    get_thermals_ = (int32_t (*)(void*, Thermals*))query_interface_(QUERY_THERMALS);
    if (!get_thermals_) {
        SPDLOG_DEBUG("NvAPI: thermals interface missing");
        return false;
    }

    // Optional: absent voltage support must not disable the temperatures.
    get_voltage_ = (int32_t (*)(void*, Voltage*))query_interface_(QUERY_VOLTAGE);

    if (!find_gpu(bus)) {
        SPDLOG_DEBUG("NvAPI: no GPU matching bus {:#x}", bus);
        return false;
    }

    if (!calculate_mask()) {
        SPDLOG_DEBUG("NvAPI: could not determine sensor mask");
        return false;
    }

    available_ = true;
    SPDLOG_DEBUG("NvAPI: sensors available (mask {:#x}, voltage {})",
                 mask_, get_voltage_ ? "yes" : "no");
    return true;
}

int NvApiSensors::read_sensor(int index) {
    if (!available_)
        return -1;

    Thermals thermals;
    memset(&thermals, 0, sizeof(thermals));
    thermals.version = (uint32_t)(sizeof(Thermals) | (2 << 16));
    thermals.mask = mask_;

    if (get_thermals_(gpu_, &thermals) != 0)
        return -1;

    int value = thermals.values[index] / FIXED_POINT_SCALE;

    // An absent sensor reads back as zero; a plausible die never does.
    if (value <= 0 || value >= 255)
        return -1;

    return value;
}

int NvApiSensors::hotspot() {
    return read_sensor(HOTSPOT_INDEX);
}

int NvApiSensors::vram() {
    return read_sensor(VRAM_INDEX);
}

int NvApiSensors::voltage() {
    if (!available_ || !get_voltage_)
        return -1;

    Voltage voltage;
    memset(&voltage, 0, sizeof(voltage));
    voltage.version = (uint32_t)(sizeof(Voltage) | (1 << 16));

    if (get_voltage_(gpu_, &voltage) != 0)
        return -1;

    int millivolts = (int)(voltage.rails[0].current_voltage_uv / MICROVOLTS_PER_MILLIVOLT);

    if (millivolts <= 0 || millivolts >= MAX_PLAUSIBLE_MILLIVOLTS)
        return -1;

    return millivolts;
}
