#pragma once
#include <cstdint>
#include <memory>
#include <mutex>

class VkCtx;
class ImGuiVK;
class ImGuiEGL;
struct HudConfig;
struct Resolution;
struct BufferSet;
enum class Backend : int32_t;

class ImGuiCtx {
  public:
    std::shared_ptr<ImGuiEGL> egl;
    inline static std::mutex global_m;
    std::mutex m;

    bool draw(uint32_t w, uint32_t h, Resolution& size, BufferSet* buf, Backend backend,
              std::shared_ptr<HudConfig> hud, std::mutex& hud_m);
    void init_vk(VkCtx* vk);
    void init_egl();
    ~ImGuiCtx();

  private:
    std::shared_ptr<ImGuiVK> vk;
};
