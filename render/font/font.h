#pragma once
#include <imgui.h>
#include <string>
#include <memory>
#include <shared_mutex>
#include "../server/config.h"

class Font {
    public:
        ImFont* small_font;
        ImFont* text_font;
        ImFont* secondary_font;
        Font(float scale_ = 1, float size_ = 24, float size_secondary_ = 24)
                 : scale(scale_), size(size_), size_secondary(size_secondary_)
        {
            // config = get_cfg();
            // TODO we need to update these when config updates
            // set_config_options();
            // needs_rebuild();
            {
                create_fonts(&atlas);
            }
        };

        // TODO this isn't fully hooked up, it will only run
        // on init and cfg might not be ready
        // void set_config_options() {
        //     cfg = get_cfg();
        //     if (cfg)
        //         size = cfg->get<int>("font_size");
        // }

        // bool needs_rebuild() {
        //     std::lock_guard lock(config->m);
        //     if (config->get<int>("font_size") != size)
        //         return true;

        //     // if (config->get<std::string>("font_file") != file)
        //     //     return true;

        //     return false;
        // }

        // void maybe_rebuild() {
        //     if (needs_rebuild()) {
        //         std::unique_lock lock(m);
        //         create_fonts(&atlas);
        //     }
        // }

        ~Font() {} ;
    private:
        ImFontAtlas atlas;
        float scale;
        float size;
        float size_secondary;
        size_t hash;
        std::string file;
        std::string file_text;
        std::shared_ptr<Config> cfg;
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
