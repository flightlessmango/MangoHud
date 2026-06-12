#include "hwmon.hpp"
#include "../common/helpers.hpp"

void HwmonBase::add_sensors(const std::vector<hwmon_sensor>& input_sensors)
{
    for (const auto& s : input_sensors) {
        sensors[s.generic_name].filename = s.filename;
        sensors[s.generic_name].label = s.label;
    }
}

void HwmonBase::find_sensors() {
    SPDLOG_DEBUG("hwmon: checking \"{}\" directory", base_dir);

    for (const auto &entry : fs::directory_iterator(base_dir)) {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        size_t underscore_pos = filename.rfind('_');

        // all hwmon sensors have underscore in their name
        // possibly needed later when checking sensor label
        if (underscore_pos == std::string::npos)
            continue;

        for (auto& s : sensors) {
            auto key = s.first;
            auto sensor = &s.second;

            if (!sensor->label.empty()) {
                std::smatch matches;
                std::regex rx(sensor->label);

                std::string label_file = filename.substr(0, underscore_pos) + "_label";
                std::string label = read_line(base_dir + "/" + label_file);

                if (!std::regex_match(label, matches, rx))
                    continue;
            }

            std::smatch matches;
            std::regex rx(sensor->filename);

            if (!std::regex_match(filename, matches, rx))
                continue;

            sensor->path = entry.path().string();
            break;
        }
    }
}

void HwmonBase::open_sensors() {
    for (auto& s : sensors) {
        auto key = s.first;
        auto sensor = &s.second;

        if (sensor->path.empty()) {
            SPDLOG_DEBUG("hwmon: {} reading not found at {}", key, base_dir);
            continue;
        }

        SPDLOG_DEBUG("hwmon: {} reading found at {}", key, sensor->path);

        sensor->stream.open(sensor->path);

        if (!sensor->stream.good()) {
            SPDLOG_DEBUG(
                "hwmon: failed to open {} reading {}",
                key, sensor->path
            );
            continue;
        }
    }
}

std::string HwmonBase::find_hwmon_dir(const std::string& drm_node) {
    std::string d = "/sys/class/drm/" + drm_node + "/device/hwmon";

    if (!fs::exists(d)) {
        SPDLOG_DEBUG("hwmon: hwmon directory \"{}\" doesn't exist", d);
        return "";
    }

    auto dir_iterator = fs::directory_iterator(d);
    auto hwmon = dir_iterator->path().string();

    if (hwmon.empty()) {
        SPDLOG_DEBUG("hwmon: hwmon directory \"{}\" is empty.", d);
        return "";
    }

    return hwmon;
}

std::string HwmonBase::find_hwmon_dir_by_name(const std::string& name) {
    std::string d = "/sys/class/hwmon/";

    if (!fs::exists(d)) {
        SPDLOG_DEBUG("hwmon: hwmon directory doesn't exist (custom linux kernel?)");
        return "";
    }

    for (const auto &entry : fs::directory_iterator(d)) {
        auto hwmon_dir = entry.path().string();
        auto hwmon_name = hwmon_dir + "/name";

        std::ifstream name_stream(hwmon_name);
        std::string name_content;

        if (!name_stream.is_open())
            continue;

        std::getline(name_stream, name_content);

        std::smatch matches;
        std::regex rx(name);

        if (!std::regex_match(name_content, matches, rx))
            continue;

        // return the first sensor with specified name
        return hwmon_dir;
    }

    SPDLOG_DEBUG("failed to find hwmon dir \"{}\"", name);
    return "";
}

void HwmonBase::setup(const std::vector<hwmon_sensor>& input_sensors, const std::string& drm_node) {
    sensors.clear();

    add_sensors(input_sensors);

    if (base_dir.empty()) {
        base_dir = find_hwmon_dir(drm_node);

        if (base_dir.empty())
            return;
    }

    find_sensors();
    open_sensors();
}

void HwmonBase::poll_sensors()
{
    for (auto& [name, sensor] : sensors) {
        if (!sensor.stream.is_open())
            continue;

        sensor.stream.clear();
        sensor.stream.seekg(0, std::ios::beg);

        std::stringstream ss;
        ss << sensor.stream.rdbuf();

        if (ss.str().empty())
            continue;

        sensor.val = std::stoull(ss.str());
    }
}

bool HwmonBase::is_exists(const std::string& generic_name) {
    return sensors.find(generic_name) != sensors.end();
}

bool HwmonBase::is_open(const std::string& generic_name) {
    if (sensors.find(generic_name) == sensors.end())
        return false;

    return sensors[generic_name].stream.is_open();
}

uint64_t HwmonBase::get_sensor_value(const std::string& generic_name) {
    if (!is_exists(generic_name)) {
        SPDLOG_DEBUG("sensor \"{}\" doesn't exist", generic_name);
        return 0;
    }

    return sensors[generic_name].val;
}

std::string HwmonBase::get_sensor_path(const std::string generic_name) {
    if (!is_exists(generic_name)) {
        SPDLOG_DEBUG("sensor \"{}\" doesn't exist", generic_name);
        return "";
    }

    return sensors[generic_name].path;
}
