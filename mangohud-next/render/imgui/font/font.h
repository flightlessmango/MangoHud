#pragma once
#include <imgui.h>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include "../server/config.h"

class Font {
    public:
        ImFont* small_font = nullptr;
        ImFont* text_font = nullptr;
        ImFont* secondary_font = nullptr;
        Font(std::function<void()> recreate_fonts_texture_ = nullptr,
             float scale_ = 1,
             float size_ = 24,
             float size_secondary_ = 24)
                 : scale(scale_),
                   size(size_),
                   size_secondary(size_secondary_),
                   recreate_fonts_texture(std::move(recreate_fonts_texture_))
        {
            // config = get_cfg();
            // TODO we need to update these when config updates
            // set_config_options();
            // needs_rebuild();
            {
                create_fonts();
            }
        };

        ImFont* get(float font_size);

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
        struct FontEntry {
            float size;
            ImFont* font;
        };

        ImFontAtlas atlas;
        float scale;
        float size;
        float size_secondary;
        size_t hash;
        std::string file;
        std::string file_text;
        std::shared_ptr<Config> cfg;
        std::vector<FontEntry> fonts;
        std::function<void()> recreate_fonts_texture;

        void create_fonts();
        ImFont* add_font(float font_size);
        void check_fonts() {
            auto hash_ = get_hash(size, size_secondary,file,
                            file_text, scale);
            if (hash != hash_) {
                create_fonts();
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
