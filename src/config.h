#pragma once
#ifndef MANGOHUD_CONFIG_H
#define MANGOHUD_CONFIG_H

#include "overlay_params.h"
#include <string>

void parseConfigFile(overlay_params& p);
std::string get_program_name();
void parseConfigLine(std::string line, std::unordered_map<std::string, std::string>& options);

\
/* NEW_FEATURE_START */ \
struct FontConfig { \
    std::string file;   /* font file path */ \
    int size = -1;      /* font size override (-1 = use global) */ \
    ImVec4 color = ImVec4(1.0f,1.0f,1.0f,1.0f); \
    bool uppercase = false; /* custom variables preserve case by default */ \
    void clear() { file.clear(); size = -1; color = ImVec4(1,1,1,1); uppercase = false; } \
}; \
\
/* new feature: extend overlay_config with per-component font configs */ \
struct PerComponentFonts { \
    FontConfig gpu; \
    FontConfig cpu; \
    FontConfig vram; \
    FontConfig ram; \
    FontConfig fps; \
    FontConfig frametime; \
    FontConfig vkd3d; \
    std::map<std::string, FontConfig> custom; /* custom_<name>_font entries */ \
}; \
/* NEW_FEATURE_END */

#endif //MANGOHUD_CONFIG_H
