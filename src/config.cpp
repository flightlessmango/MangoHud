#include <fstream>
#include <sstream>
#include <iostream>
#include <string.h>
#include <thread>
#include <unordered_map>
#include <string>
#include <spdlog/spdlog.h>
#include "config.h"
#include "file_utils.h"
#include "string_utils.h"
#include "hud_elements.h"

static void parseConfigLine(std::string line, std::unordered_map<std::string, std::string>& options) {
    std::string param, value;

    if (line.find("#") != std::string::npos)
        line = line.erase(line.find("#"), std::string::npos);

    size_t equal = line.find("=");
    if (equal == std::string::npos)
        value = "1";
    else
        value = line.substr(equal+1);

    param = line.substr(0, equal);
    trim(param);
    trim(value);
    if (!param.empty()){
        HUDElements.options.push_back({param, value});
        options[param] = value;
    }
}

static std::string get_program_dir() {
    const std::string exe_path = get_exe_path();
    if (exe_path.empty()) {
        return std::string();
    }
    const auto n = exe_path.find_last_of('/');
    if (n != std::string::npos) {
        return exe_path.substr(0, n);
    }
    return std::string();
}

std::string get_program_name() {
    const std::string exe_path = get_exe_path();
    std::string basename = "unknown";
    if (exe_path.empty()) {
        return basename;
    }
    const auto n = exe_path.find_last_of('/');
    if (n == std::string::npos) {
        return basename;
    }
    if (n < exe_path.size() - 1) {
        // An executable's name.
        basename = exe_path.substr(n + 1);
    }
    return basename;
}

static void enumerate_config_files(std::vector<std::string>& paths) {
    static const char *mangohud_dir = "/MangoHud/";

    const std::string data_dir = get_data_dir();
    const std::string config_dir = get_config_dir();

    const std::string program_name = get_program_name();

    if (config_dir.empty()) {
        // If we can't find 'HOME' just abandon hope.
        return;
    }

    paths.push_back(config_dir + mangohud_dir + "MangoHud.conf");

    if (!program_name.empty()) {
        paths.push_back(config_dir + mangohud_dir + program_name + ".conf");
    }

    const std::string program_dir = get_program_dir();
    if (!program_dir.empty()) {
        paths.push_back(program_dir + "/MangoHud.conf");
    }

    const std::string wine_program_name = get_wine_exe_name();
    if (!wine_program_name.empty()) {
        paths.push_back(config_dir + mangohud_dir + "wine-" + wine_program_name + ".conf");
     }
}

bool parseConfigFile(overlay_params& params) {
    HUDElements.options.clear();
    params.options.clear();
    std::vector<std::string> paths;
    const char *cfg_file = getenv("MANGOHUD_CONFIGFILE");

    if (cfg_file)
        paths.push_back(cfg_file);
    else
        enumerate_config_files(paths);
#ifdef _WIN32
    paths.push_back("C:\\mangohud\\MangoHud.conf");
#endif
    std::string line;
    for (auto p = paths.rbegin(); p != paths.rend(); p++) {
        std::ifstream stream(*p);
        if (!stream.good()) {
            // printing just so user has an idea of possible configs
            SPDLOG_INFO("skipping config: '{}' [ not found ]", *p);
            continue;
        }

        stream.imbue(std::locale::classic());
        SPDLOG_INFO("parsing config: '{}'", *p);
        while (std::getline(stream, line))
        {
            parseConfigLine(line, params.options);
        }
        params.config_file_path = *p;
        return true;
    }

    return false;
}
