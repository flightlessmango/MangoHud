#pragma once

#include <string>
#include <fstream>
#include <cstdint>
#include <map>
#include <regex>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "gpu/gpu.hpp"

namespace fs = std::filesystem;

struct hwmon_sensor {
    std::string generic_name;
    std::string filename;
    std::string label;
};

class HwmonBase {
private:
    struct sensor {
        std::string filename;
        std::string label;

        std::ifstream stream;
        std::string path;
        unsigned char id = 0;
        uint64_t val = 0;
    };

    std::map<std::string, sensor> sensors;
    void add_sensors(const std::vector<hwmon_sensor>& input_sensors);
    void find_sensors();
    void open_sensors();

public:
    std::string base_dir;

    std::string find_hwmon_dir(const std::string& drm_node);
    std::string find_hwmon_dir_by_name(const std::string& name);

    void setup(const std::vector<hwmon_sensor>& input_sensors, const std::string& drm_node = "");
    void poll_sensors();

    bool is_exists(const std::string& generic_name);
    bool is_open(const std::string& generic_name);
    uint64_t get_sensor_value(const std::string& generic_name);
    std::string get_sensor_path(const std::string generic_name);
};

struct Hwmon {
    HwmonBase hwmon;
};
