#pragma once
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include "../ipc/proto.h"
#include "../server/config.h"

struct dmabuf {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view;
};

struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    uint32_t width = 0, height = 0;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct frame {
    uint32_t w,h;
    HudTable table;
    RenderTarget target;
    VkImage image;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    int num;
};

class VulkanContext {
public:
  VulkanContext() {
    init(true);
  };

  void init(bool enableValidation);
  int phys_fd();
  uint32_t find_mem_type(uint32_t type_bits);
  dmabuf create_dmabuf_image(GbmBuffer& gbm);
  RenderTarget create_render_target(uint32_t w, uint32_t h, VkFormat fmt);

  void create_dmabuf(GbmBuffer& gbm, RenderTarget& target,
                     dmabuf& buf, uint32_t w, uint32_t h, VkFormat fmt) {
    buf = create_dmabuf_image(gbm);
    target = create_render_target(w, h, fmt);
  };

  void teardown();

  ~VulkanContext() { teardown(); }

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
  VkDevice device() const { return device_; }
  VkQueue graphicsQueue() const { return graphicsQueue_; }
  uint32_t graphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex_; }
  VkDebugUtilsMessengerEXT debugMessenger() const { return debugMessenger_; }

private:
  bool initialized_ = false;

  vkb::Instance vkbInstance_{};
  vkb::PhysicalDevice vkbPhysicalDevice_{};
  vkb::Device vkbDevice_{};

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  uint32_t graphicsQueueFamilyIndex_ = UINT32_MAX;
};
