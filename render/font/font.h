#pragma once
#include <imgui.h>
#include <string>
#include <memory>
#include "../server/config.h"

class Font {
    public:
        Font(float scale_ = 1, float size_ = 24, float size_secondary_ = 24)
                 : scale(scale_), size(size_), size_secondary(size_secondary_)
        {
            // TODO we need to update these when config updates
            set_config_options();
            create_fonts(&atlas);
        };

        // TODO this isn't fully hooked up, it will only run
        // on init and cfg might not be ready
        void set_config_options() {
            auto cfg = get_cfg();
            if (cfg)
                size = cfg->get<int>("font_size");
        }

        ~Font() {} ;

        ImFontAtlas atlas;
        ImFont* small_font;
        ImFont* text_font;
        ImFont* secondary_font;
    private:
        float scale;
        float size;
        float size_secondary;
        size_t hash;
        std::string file;
        std::string file_text;
        void create_fonts(ImFontAtlas* atlas);
        void check_fonts() {
            auto hash_ = get_hash(size, size_secondary,file,
                            file_text, scale);
            if (hash != hash_) {
                create_fonts(&atlas);
                hash = hash_;
            }

        }

        template<typename... Ts>
        size_t get_hash(Ts const&... args) {
            size_t h = 0;
            ((h ^= (std::hash<Ts>{}(args) << 1)), ...);
            return h;
        }
};
