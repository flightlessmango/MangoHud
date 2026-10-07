#pragma once
#include <cstdint>

// Reads sensors that NVML does not expose on GeForce cards by going through
// NVIDIA's own NVAPI library (libnvidia-api.so.1). No elevated privileges and
// no direct BAR0 access are needed: the driver performs the read for us.
//
// Blackwell reports the hotspot through a different path and is deliberately not
// handled - hotspot() simply reports nothing there rather than a wrong number.
class NvApiSensors {
    public:
        ~NvApiSensors();

        // pci_bus_id is the usual "0000:01:00.0" form.
        bool init(const char* pci_bus_id);
        bool available() const { return available_; }

        // Integer degrees celsius, truncated, or -1 when unavailable.
        int hotspot();
        int vram();

        // Millivolts, or -1 when unavailable.
        int voltage();

    private:
        struct Thermals {
            uint32_t version;
            int32_t mask;
            int32_t values[40];
        };

        struct VoltageRail {
            uint32_t rail_id;
            uint32_t current_voltage_uv;
            uint8_t reserved[32];
        };

        struct Voltage {
            uint32_t version;
            uint8_t reserved[32];
            VoltageRail rails[1];
        };

        static const int HOTSPOT_INDEX = 9;

        // Verified on GA107 (Ampere, GDDR6) by comparing a shader-bound load with
        // a memory-bound one at matched core temperature: every die sensor stayed
        // within 0.16 C of the core across both, while this one rose 1.24 C once
        // the memory bus was saturated. It also reports whole degrees only, unlike
        // the 1/256 C die taps. Other parts may well place memory elsewhere.
        static const int VRAM_INDEX = 16;

        void* library_ = nullptr;
        void* (*query_interface_)(uint32_t) = nullptr;
        int32_t (*get_thermals_)(void*, Thermals*) = nullptr;
        int32_t (*get_voltage_)(void*, Voltage*) = nullptr;
        void* gpu_ = nullptr;
        int32_t mask_ = 0;
        bool available_ = false;

        bool find_gpu(unsigned bus);
        bool calculate_mask();
        int read_sensor(int index);
};
