#include <fstream>
#include <sstream>
#include <iostream>
#include <string.h>
#include <thread>
#include "config.h"
#include "file_utils.h"
#include "string_utils.h"

void parseConfigLine(std::string line, std::unordered_map<std::string,std::string>& options) {
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
    if (!param.empty())
        options[param] = value;
}

void enumerate_config_files(std::vector<std::string>& paths)
{
    static const char *mangohud_dir = "/MangoHud/";

    std::string env_data = get_data_dir();
    std::string env_config = get_config_dir();

    if (!env_config.empty())
        paths.push_back(env_config + mangohud_dir + "MangoHud.conf");

    std::string exe_path = get_exe_path();
    auto n = exe_path.find_last_of('/');
    if (!exe_path.empty() && n != std::string::npos && n < exe_path.size() - 1) {
        // as executable's name
        std::string basename = exe_path.substr(n + 1);
        if (!env_config.empty())
            paths.push_back(env_config + mangohud_dir + basename + ".conf");

        // in executable's folder though not much sense in /usr/bin/
        paths.push_back(exe_path.substr(0, n) + "/MangoHud.conf");

        // find executable's path when run in Wine
        if (!env_config.empty() && (basename == "wine-preloader" || basename == "wine64-preloader")) {
            std::string name;
            if (get_wine_exe_name(name)) {
                paths.push_back(env_config + mangohud_dir + "wine-" + name + ".conf");
            }
        }
    }
}

void parseConfigFile(overlay_params& params) {
    params.options.clear();
    std::vector<std::string> paths;
    const char *cfg_file = getenv("MANGOHUD_CONFIGFILE");

    if (cfg_file)
        paths.push_back(cfg_file);
    else
        enumerate_config_files(paths);

    std::string line;
    for (auto p = paths.rbegin(); p != paths.rend(); p++) {
        std::ifstream stream(*p);
        if (!stream.good()) {
            // printing just so user has an idea of possible configs
            std::cerr << "skipping config: " << *p << " [ not found ]" << std::endl;
            continue;
        }

        stream.imbue(std::locale::classic());
        std::cerr << "parsing config: " << *p;
        while (std::getline(stream, line))
        {
            parseConfigLine(line, params.options);
        }
        std::cerr << " [ ok ]" << std::endl;
        params.config_file_path = *p;
        return;
    }
}
