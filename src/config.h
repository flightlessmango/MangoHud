#include <unordered_map>
#include <string>

extern std::unordered_map<std::string,std::string> options;
extern std::string config_file_path;

void parseConfigFile(void);