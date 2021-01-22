#include <fstream>
#include <sstream>
#include <iostream>
#include <string.h>
#include <thread>
#include <string>
#include "config.h"
#include "file_utils.h"
#include "string_utils.h"
#include "hud_elements.h"

std::vector<std::string> used_configs;
static const char *mangohud_dir = "/MangoHud/";

void enumerate_config_files(std::vector<std::string>& paths, std::string configName, bool subconfig);
bool parseConfigFile(overlay_params& params, std::string path);

static void parseConfigLine(std::string line, overlay_params& params) {
    std::string param, value;

    if (line.find("#") != std::string::npos)
        line = line.erase(line.find("#"), std::string::npos);

    size_t equal = line.find("=");
    bool no_value = equal == std::string::npos;
    if (no_value)
        value = "1";
    else
        value = line.substr(equal+1);

    param = line.substr(0, equal);
    trim(param);
    trim(value);
    if (!param.empty()){
        if (param == "include_cfg") {
            if (value.empty() || no_value) { // No config specified - try to include MangoHud.conf
                if (parseConfigFile(params, get_config_dir() + mangohud_dir + "MangoHud.conf")) {
                    return;
                }
            } else {
                if (value.at(0) == '/') { // example: /home/user/MangoHud.conf
                    if (parseConfigFile(params, value)) {
                        return;
                    }
                } else { // example: MangoHud.conf
                    if (parseConfigFile(params, get_config_dir() + mangohud_dir + value)) {
                        return;
                    }
                }
            }

            std::cerr << "MANGOHUD: Failed to find a config to include: " << value << std::endl;
            return;
        }

        HUDElements.options.push_back({param, value});
        params.options[param] = value;
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

#ifdef _WIN32
    paths.push_back("C:\\mangohud\\MangoHud.conf");
#endif

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

bool parseConfigFile(overlay_params& params, std::string path) {
    for (std::string used_config : used_configs) {
        if (path == used_config) {
            std::cerr << "config already used: " << path << std::endl;
            return false;
        }
    };

    std::string line;

    std::ifstream stream(path);
    if (!stream.good()) {
        // printing just so user has an idea of possible configs
        std::cerr << "skipping config: " << path << " [ not found ]" << std::endl;
        return false;
    }
    used_configs.push_back(path);
    
    stream.imbue(std::locale::classic());
    std::cerr << "parsing config: " << path << std::endl;
    while (std::getline(stream, line))
    {
        parseConfigLine(line, params);
    }
    std::cerr << "parsed config: " << path << " [ ok ]" << std::endl;
    return true;
}


void parseConfigFiles(overlay_params& params) {
    HUDElements.options.clear();
    params.options.clear();
    used_configs.clear();

    std::vector<std::string> paths;
    const char *cfg_file = getenv("MANGOHUD_CONFIGFILE");

    if (cfg_file)
        paths.push_back(cfg_file);
    else
        enumerate_config_files(paths);

    std::string line;
    for (auto p = paths.rbegin(); p != paths.rend(); p++) {
        if (parseConfigFile(params, *p)) {
            params.config_file_path = *p;
            return;
        }
    }
}