#include <fstream>
#include <sstream>
#include <iostream>
#include <string.h>
#include "config.h"
#include "overlay_params.h"
#include <vector>
std::vector<const char*> config_params;

void configParams(){
    config_params.push_back("height");
}

void parseConfigLine(std::string line, struct overlay_params *params){
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
            return;
        }
    
        std::cout  << "set option " << line.substr(0,equal) << " equal to " << line.substr(equal+1) << std::endl;
}

void parseConfigFile(struct overlay_params *params) {
    configParams();
    std::string home = std::getenv("HOME");
    std::string filePath = home + "/.local/share/MangoHud/MangoHud.conf";
    std::ifstream stream(filePath);

    std::string line;

    while (std::getline(stream, line))
    {
        parseConfigLine(line, params);
    }
}