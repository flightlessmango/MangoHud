#include <stdexcept>
#include <string>
#include <fcntl.h>
#include "../ipc/ipc.h"
#include "vulkan_ctx.h"

void VulkanContext::init(bool enableValidation = true) {
  vkb::InstanceBuilder ib;
  ib.set_app_name("Server")
    .set_engine_name("NoEngine")
    .require_api_version(1, 3, 0);

  if (enableValidation) {
    ib.request_validation_layers(true)
      .use_default_debug_messenger();
  }

  auto instRet = ib.build();
  if (!instRet) {
    throw std::runtime_error(std::string("Failed to create Vulkan instance: ")
                              + instRet.error().message());
  }

  vkbInstance_ = instRet.value();
  instance_ = vkbInstance_.instance;
  debugMessenger_ = enableValidation ? vkbInstance_.debug_messenger : VK_NULL_HANDLE;

  vkb::PhysicalDeviceSelector selector{vkbInstance_};
  VkPhysicalDeviceVulkan13Features f13{};
  f13.dynamicRendering = VK_TRUE;
  selector.set_required_features_13(f13);
  selector.set_minimum_version(1, 3);
  selector.require_present(false);
  selector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);
  selector.add_required_extension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
  selector.add_required_extension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
  selector.add_required_extension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
  selector.add_required_extension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
  selector.add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

  auto physRet = selector.select();
  if (!physRet) {
    throw std::runtime_error(std::string("Failed to select physical device: ")
                            + physRet.error().message());
  }

  vkbPhysicalDevice_ = physRet.value();
  physicalDevice_ = vkbPhysicalDevice_.physical_device;

  vkb::DeviceBuilder db{vkbPhysicalDevice_};
  auto devRet = db.build();
  if (!devRet) {
    throw std::runtime_error(std::string("Failed to create logical device: ")
                              + devRet.error().message());
  }

  vkbDevice_ = devRet.value();
  device_ = vkbDevice_.device;

  auto gq = vkbDevice_.get_queue(vkb::QueueType::graphics);
  if (!gq) {
    throw std::runtime_error(std::string("Failed to get graphics queue: ")
                              + gq.error().message());
  }
  graphicsQueue_ = gq.value();

  auto gqfi = vkbDevice_.get_queue_index(vkb::QueueType::graphics);
  if (!gqfi) {
    throw std::runtime_error(std::string("Failed to get graphics queue index: ")
                              + gqfi.error().message());
  }
  graphicsQueueFamilyIndex_ = gqfi.value();

  initialized_ = true;
}

int VulkanContext::phys_fd() {
  VkPhysicalDeviceDrmPropertiesEXT drm{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
  };
  VkPhysicalDeviceProperties2 props2{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &drm,
  };
  vkGetPhysicalDeviceProperties2(physicalDevice(), &props2);
  if (!drm.hasRender)
    return -1;

  std::string path = "/dev/dri/renderD" + std::to_string(drm.renderMinor);
  int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
  if (fd < 0) throw std::runtime_error("Failed to open " + path);
  return fd;
}

uint32_t VulkanContext::find_mem_type(uint32_t type_bits) {
  VkPhysicalDeviceMemoryProperties mp{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &mp);

  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      if (type_bits & (1u << i)) return i;
  }
  return UINT32_MAX;
}

dmabuf VulkanContext::create_dmabuf_image(GbmBuffer& gbm) {
  dmabuf out{};
  VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  VkExternalMemoryImageCreateInfo extImg{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
  };

  VkSubresourceLayout plane0{
      .offset = gbm.offset,
      .size = 0,
      .rowPitch = gbm.stride,
      .arrayPitch = 0,
      .depthPitch = 0,
  };

  VkImageDrmFormatModifierExplicitCreateInfoEXT drmExplicit{
      .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
      .pNext = &extImg,
      .drmFormatModifier = gbm.modifier,
      .drmFormatModifierPlaneCount = 1,
      .pPlaneLayouts = &plane0,
  };

  VkImageCreateInfo ici{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &drmExplicit,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .extent = { gbm.width, gbm.height, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  vkCreateImage(device(), &ici, nullptr, &out.image);
  VkImageMemoryRequirementsInfo2 info2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .pNext = nullptr,
      .image = out.image,
  };

  VkMemoryDedicatedRequirements dedicatedReq{
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
      .pNext = nullptr,
  };

  VkMemoryRequirements2 req2{
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
      .pNext = &dedicatedReq,
  };

  vkGetImageMemoryRequirements2(device(), &info2, &req2);

  const int import_fd = dup(gbm.fd);

  VkImportMemoryFdInfoKHR importInfo{
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = nullptr,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = import_fd,
  };

  VkMemoryDedicatedAllocateInfo dedicatedAlloc{
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = nullptr,
      .image = out.image,
      .buffer = VK_NULL_HANDLE,
  };

  if (dedicatedReq.requiresDedicatedAllocation || dedicatedReq.prefersDedicatedAllocation) {
      importInfo.pNext = &dedicatedAlloc;
  }

  const uint32_t memType = find_mem_type(req2.memoryRequirements.memoryTypeBits);

  VkMemoryAllocateInfo mai{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &importInfo,
      .allocationSize = req2.memoryRequirements.size,
      .memoryTypeIndex = memType,
  };

  vkAllocateMemory(device(), &mai, nullptr, &out.memory);
  vkBindImageMemory(device(), out.image, out.memory, 0);

  VkImageViewCreateInfo vci{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = out.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
      },
  };

  vkCreateImageView(device(), &vci, nullptr, &out.view);
  return out;
}

RenderTarget VulkanContext::create_render_target(uint32_t w, uint32_t h, VkFormat fmt) {
    RenderTarget rt{};
    rt.width = w;
    rt.height = h;
    rt.format = fmt;
    rt.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCreateImage(device(), &ci, nullptr, &rt.image);

    VkImageMemoryRequirementsInfo2 info2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = rt.image,
    };

    VkMemoryDedicatedRequirements dedicatedReq{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };

    VkMemoryRequirements2 req2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicatedReq,
    };
    vkGetImageMemoryRequirements2(device(), &info2, &req2);

    VkMemoryDedicatedAllocateInfo dedicatedAlloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = rt.image,
        .buffer = VK_NULL_HANDLE,
    };

    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = dedicatedReq.requiresDedicatedAllocation ? &dedicatedAlloc : nullptr,
        .allocationSize = req2.memoryRequirements.size,
        .memoryTypeIndex = find_mem_type(req2.memoryRequirements.memoryTypeBits)
    };

    VkDeviceMemory mem = VK_NULL_HANDLE;
    vkAllocateMemory(device(), &allocInfo, nullptr, &mem);
    vkBindImageMemory(device(), rt.image, mem, 0);

    // View
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = rt.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;

    vkCreateImageView(device(), &vi, nullptr, &rt.view);

    return rt;
}
void VulkanContext::teardown() {
  if (!initialized_) return;

  VkDevice dev = vkbDevice_.device;
  if (dev != VK_NULL_HANDLE) vkDeviceWaitIdle(dev);

  if (dev != VK_NULL_HANDLE) {
    vkb::destroy_device(vkbDevice_);
    vkbDevice_ = {};
  }
  if (vkbInstance_.instance != VK_NULL_HANDLE) {
    vkb::destroy_instance(vkbInstance_);
    vkbInstance_ = {};
  }

  initialized_ = false;
}
