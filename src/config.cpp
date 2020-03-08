#include <fstream>
#include <sstream>
#include <iostream>
#include <string.h>
#include "config.h"
#include "overlay_params.h"
#include "file_utils.h"

std::unordered_map<std::string,std::string> options;

void parseConfigLine(std::string line){
    if(line.find("#")!=std::string::npos)
        {
            line = line.erase(line.find("#"),std::string::npos);
        }
    size_t space = line.find(" ");
    while(space!=std::string::npos)
        {
            line = line.erase(space,1);
            space = line.find(" ");
        }
    space = line.find("\t");
    while(space!=std::string::npos)
        {
            line = line.erase(space,1);
            space = line.find("\t");
        }
    size_t equal = line.find("=");
    if(equal==std::string::npos)
        {
            if (!line.empty())
                options[line] = "1";
            return;
        }
    
    options[line.substr(0, equal)] = line.substr(equal+1);
}

void parseConfigFile() {
    std::vector<std::string> paths;
    static const char *mangohud_dir = "/MangoHud/";

    std::string env_data = get_data_dir();
    std::string env_config = get_config_dir();

    if (!env_data.empty())
        paths.push_back(env_data + mangohud_dir + "MangoHud.conf");

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
            std::string line;
            std::ifstream stream("/proc/self/cmdline");
            while (std::getline(stream, line, '\0'))
            {
                if (!line.empty()
                    && (n = line.find_last_of("/\\")) != std::string::npos
                    && n < line.size() - 1) // have at least one character
                {
                    auto dot = line.find_last_of('.');
                    if (dot < n)
                        dot = line.size();
                    paths.push_back(env_config + mangohud_dir + "wine-" + line.substr(n + 1, dot - n - 1) + ".conf");
                    break;
                }
            }
        }
    }

    std::string line;
    for (auto p = paths.rbegin(); p != paths.rend(); p++) {
        std::ifstream stream(*p);
        if (!stream.good()) {
            // printing just so user has an idea of possible configs
            std::cerr << "skipping config: " << *p << std::endl;
            continue;
        }

        std::cerr << "parsing config: " << *p;
        while (std::getline(stream, line))
        {
            parseConfigLine(line);
        }
        std::cerr << " [ ok ]" << std::endl;
        return;
    }
}
