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
                options.insert({line, "1"});
            return;
        }
    
    options.insert({line.substr(0,equal), line.substr(equal+1)});
}

void parseConfigFile() {
    std::vector<std::string> paths;
    std::string home;
    static const char *config_dir = "/.config/MangoHud";

    const char *env_home = std::getenv("HOME");
    if (env_home)
        home = env_home;
    if (!home.empty()) {
        paths.push_back(home + "/.local/share/MangoHud/MangoHud.conf");
        paths.push_back(home + config_dir + "/MangoHud.conf");
    }

    std::string exe_path = get_exe_path();
    auto n = exe_path.find_last_of('/');
    if (!exe_path.empty() && n != std::string::npos) {
        // as executable's name
        if (!home.empty())
            paths.push_back(home + config_dir + exe_path.substr(n) + ".conf");

        // in executable's folder though not much sense in /usr/bin/
        paths.push_back(exe_path.substr(0, n) + "/MangoHud.conf");
    }

    std::string line;
    for (auto& p : paths) {
        std::cerr << "parsing config: " << p << std::endl;
        std::ifstream stream(p);
        while (std::getline(stream, line))
        {
            parseConfigLine(line);
        }
    }
}