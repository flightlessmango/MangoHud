#include <fstream>
#include <sstream>
#include <iostream>
#include <string.h>
#include "config.h"
#include "overlay_params.h"
#include <vector>
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
            return;
        }
    
    options.insert({line.substr(0,equal), line.substr(equal+1)});
}

void parseConfigFile() {
    std::string home = std::getenv("HOME");
    std::string filePath = home + "/.local/share/MangoHud/MangoHud.conf";
    std::ifstream stream(filePath);

    std::string line;

    while (std::getline(stream, line))
    {
        parseConfigLine(line);
    }
}