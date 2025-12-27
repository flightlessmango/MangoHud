#define VKROOTS_LAYER_IMPLEMENTATION
#include "vkroots.h"

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>

#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <drm/drm_fourcc.h>
#include "mesa/os_time.h"
#include "../ipc/ipc_client.h"
IPCClient client;

namespace MyLayer {

static inline void logf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
}

// ----------------------------
// Embedded SPIR-V
// ----------------------------
static const uint32_t overlay_vert_spv[] = {
#include "overlay.vert.spv.h"
};
static const uint32_t overlay_frag_spv[] = {
#include "overlay.frag.spv.h"
};

// ----------------------------
// Device state
// ----------------------------

struct SwapchainState {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{0, 0};

  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  bool imagesInitialized = false;
};

struct ImportedImage {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{0, 0};

  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;

  bool valid() const {
    return image != VK_NULL_HANDLE && memory != VK_NULL_HANDLE && view != VK_NULL_HANDLE;
  }
};

struct CmdObjects {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
};

struct DeviceState {
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;

  VkPhysicalDeviceMemoryProperties memProps{};
  bool memPropsValid = false;

  std::unordered_map<VkSwapchainKHR, SwapchainState> swapchains;
  std::unordered_map<uint32_t, CmdObjects> cmdByFamily;

  // dma-buf
  GbmBuffer gbm{};
  bool importedReady = false;
  ImportedImage imported{};
  VkImageLayout importedLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  // overlay descriptors
  VkSampler overlaySampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout overlayDSL = VK_NULL_HANDLE;
  VkPipelineLayout overlayPL = VK_NULL_HANDLE;
  VkDescriptorPool overlayDP = VK_NULL_HANDLE;
  VkDescriptorSet overlayDS = VK_NULL_HANDLE;

  // shaders
  VkShaderModule overlayVS = VK_NULL_HANDLE;
  VkShaderModule overlayFS = VK_NULL_HANDLE;

  // Vulkan 1.0 rendering objects
  std::unordered_map<VkFormat, VkRenderPass> overlayRpByFormat;
  std::unordered_map<VkFormat, VkPipeline>   overlayPipeByFormat;
  std::unordered_map<VkImageView, VkFramebuffer> overlayFbByView;

  // present chaining
  VkSemaphore overlayDone = VK_NULL_HANDLE;
};

struct QueueMapEntry {
  DeviceState* ds = nullptr;
  uint32_t family = 0;
};

static std::mutex g_mtx;
static std::unordered_map<VkDevice, std::unique_ptr<DeviceState>> g_devices;

static std::mutex g_qmtx;
static std::unordered_map<VkQueue, QueueMapEntry> g_queue_to_entry;

static DeviceState* get_ds(VkDevice dev) {
  std::lock_guard<std::mutex> lock(g_mtx);
  auto it = g_devices.find(dev);
  if (it == g_devices.end()) return nullptr;
  return it->second.get();
}

static DeviceState* get_or_create_ds(VkDevice dev) {
  std::lock_guard<std::mutex> lock(g_mtx);
  auto& ptr = g_devices[dev];
  if (!ptr) ptr = std::make_unique<DeviceState>();
  ptr->device = dev;
  return ptr.get();
}

static void erase_ds(VkDevice dev) {
  std::lock_guard<std::mutex> lock(g_mtx);
  g_devices.erase(dev);
}

static bool drm_fourcc_to_vk_format(uint32_t fourcc, VkFormat* out) {
  switch (fourcc) {
    case DRM_FORMAT_ARGB8888: // "AR24"
    case DRM_FORMAT_XRGB8888:
      *out = VK_FORMAT_B8G8R8A8_SRGB;
      return true;

    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
      *out = VK_FORMAT_B8G8R8A8_SRGB;
      return true;

    default:
      return false;
  }
}

static void fill_mem_props(const vkroots::VkDeviceDispatch* d, DeviceState& ds) {
  if (ds.memPropsValid) return;
  VkPhysicalDeviceMemoryProperties props{};
  d->pPhysicalDeviceDispatch->pInstanceDispatch->GetPhysicalDeviceMemoryProperties(ds.phys, &props);
  ds.memProps = props;
  ds.memPropsValid = true;
}

static uint32_t find_mem_type_importable(const DeviceState& ds, uint32_t typeBits) {
  if (!ds.memPropsValid) return UINT32_MAX;
  const VkMemoryPropertyFlags bad =
    VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

  for (uint32_t i = 0; i < ds.memProps.memoryTypeCount; i++) {
    if (!(typeBits & (1u << i))) continue;
    VkMemoryPropertyFlags flags = ds.memProps.memoryTypes[i].propertyFlags;
    if ((flags & bad) != 0) continue;
    if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) return i;
  }
  for (uint32_t i = 0; i < ds.memProps.memoryTypeCount; i++) {
    if (!(typeBits & (1u << i))) continue;
    VkMemoryPropertyFlags flags = ds.memProps.memoryTypes[i].propertyFlags;
    if ((flags & bad) != 0) continue;
    return i;
  }
  return UINT32_MAX;
}

static CmdObjects* ensure_cmd_objects_for_family(const vkroots::VkDeviceDispatch* d,
                                                 DeviceState& ds,
                                                 uint32_t family)
{
  auto& co = ds.cmdByFamily[family];
  if (co.pool && co.cmd && co.fence) return &co;

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = family;

  if (d->CreateCommandPool(ds.device, &pci, nullptr, &co.pool) != VK_SUCCESS)
    return nullptr;

  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  if (d->CreateFence(ds.device, &fci, nullptr, &co.fence) != VK_SUCCESS)
    return nullptr;

  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = co.pool;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = 1;

  if (d->AllocateCommandBuffers(ds.device, &cai, &co.cmd) != VK_SUCCESS)
    return nullptr;

  return &co;
}

static void destroy_imported(const vkroots::VkDeviceDispatch* d, VkDevice dev, ImportedImage& imp) {
  if (imp.view)   d->DestroyImageView(dev, imp.view, nullptr);
  if (imp.image)  d->DestroyImage(dev, imp.image, nullptr);
  if (imp.memory) d->FreeMemory(dev, imp.memory, nullptr);
  imp = {};
}

static bool update_overlay_descriptor(const vkroots::VkDeviceDispatch* d, DeviceState& ds)
{
  if (ds.imported.view == VK_NULL_HANDLE || ds.overlayDS == VK_NULL_HANDLE) return false;

  VkDescriptorImageInfo ii{};
  ii.sampler = ds.overlaySampler;
  ii.imageView = ds.imported.view;
  ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = ds.overlayDS;
  w.dstBinding = 0;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  w.pImageInfo = &ii;

  d->UpdateDescriptorSets(ds.device, 1, &w, 0, nullptr);
  return true;
}

static bool import_dmabuf_from_gbm(const vkroots::VkDeviceDispatch* d,
                                  DeviceState& ds,
                                  const GbmBuffer& gbm,
                                  ImportedImage* out)
{
  if (!out) return false;
  *out = {}; // clear output

  fill_mem_props(d, ds);

  if (gbm.fd < 0) return false;

  VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;

  // --- Create image with external memory + DRM modifier explicit ---
  VkExternalMemoryImageCreateInfo extImg{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  extImg.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

  VkSubresourceLayout plane{};
  plane.offset   = gbm.offset;
  plane.rowPitch = gbm.stride;
  plane.size     = gbm.plane_size;

  VkImageDrmFormatModifierExplicitCreateInfoEXT drmExp{
    VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT
  };
  drmExp.drmFormatModifier = gbm.modifier;
  drmExp.drmFormatModifierPlaneCount = 1;
  drmExp.pPlaneLayouts = &plane;
  drmExp.pNext = &extImg;

  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.pNext = &drmExp;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = fmt;
  ici.extent = { gbm.width, gbm.height, 1 };
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
  ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage img = VK_NULL_HANDLE;
  if (d->CreateImage(ds.device, &ici, nullptr, &img) != VK_SUCCESS)
    return false;

  VkMemoryRequirements mr{};
  d->GetImageMemoryRequirements(ds.device, img, &mr);

  if (!d->GetMemoryFdPropertiesKHR) {
    logf("[mylayer] import: missing VK_KHR_external_memory_fd (GetMemoryFdPropertiesKHR null)\n");
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  // Query fd properties (needs a dup, since the call consumes nothing but uses fd)
  int fd_query = dup(gbm.fd);
  if (fd_query < 0) {
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  VkMemoryFdPropertiesKHR fdProps{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
  VkResult r = d->GetMemoryFdPropertiesKHR(
    ds.device,
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    fd_query,
    &fdProps
  );
  close(fd_query);

  if (r != VK_SUCCESS) {
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  uint32_t both = mr.memoryTypeBits & fdProps.memoryTypeBits;
  uint32_t memType = find_mem_type_importable(ds, both);
  if (memType == UINT32_MAX) {
    logf("[mylayer] import: no mem type both=0x%08x\n", both);
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  // This fd WILL be consumed by vkAllocateMemory on success
  int fd_for_vulkan = dup(gbm.fd);
  if (fd_for_vulkan < 0) {
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  VkImportMemoryFdInfoKHR importFd{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
  importFd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  importFd.fd = fd_for_vulkan;

  VkMemoryDedicatedAllocateInfo dedi{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedi.image = img;
  dedi.pNext = &importFd;

  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.pNext = &dedi;
  mai.allocationSize = (gbm.plane_size) ? (VkDeviceSize)gbm.plane_size : mr.size;
  mai.memoryTypeIndex = memType;

  VkDeviceMemory mem = VK_NULL_HANDLE;
  if (d->AllocateMemory(ds.device, &mai, nullptr, &mem) != VK_SUCCESS) {
    // fd not consumed on failure
    close(fd_for_vulkan);
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  // If BindImageMemory fails, mem is ours to free
  if (d->BindImageMemory(ds.device, img, mem, 0) != VK_SUCCESS) {
    d->FreeMemory(ds.device, mem, nullptr);
    d->DestroyImage(ds.device, img, nullptr);
    return false;
  }

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = img;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = fmt;
  vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vci.subresourceRange.levelCount = 1;
  vci.subresourceRange.layerCount = 1;

  VkImageView view = VK_NULL_HANDLE;
  if (d->CreateImageView(ds.device, &vci, nullptr, &view) != VK_SUCCESS) {
    d->DestroyImage(ds.device, img, nullptr);
    d->FreeMemory(ds.device, mem, nullptr);
    return false;
  }

  out->format = fmt;
  out->extent = { gbm.width, gbm.height };
  out->image = img;
  out->memory = mem;
  out->view = view;

  return true;
}

static bool same_gbm_buffer(const GbmBuffer& a, const GbmBuffer& b) {
  return a.fd == b.fd &&
         a.modifier == b.modifier &&
         a.stride == b.stride &&
         a.offset == b.offset &&
         a.fourcc == b.fourcc &&
         a.plane_size == b.plane_size &&
         a.width == b.width &&
         a.height == b.height;
}

static bool refresh_imported_if_needed(const vkroots::VkDeviceDispatch* d, DeviceState& ds) {
  GbmBuffer cur = client.gbm;

  // disconnected: free and mark not-ready
  if (cur.fd < 0) {
    if (ds.importedReady) {
      destroy_imported(d, ds.device, ds.imported);
      ds.importedReady = false;
      ds.importedLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      ds.gbm = {}; // optional: clear cached metadata
    }
    return true;
  }

  // first time or changed: import new, then destroy old
  bool changed = (!ds.importedReady) || !same_gbm_buffer(ds.gbm, cur);
  ds.gbm = client.gbm;
  if (!changed)
    return true;

  ImportedImage newImp{};
  if (!import_dmabuf_from_gbm(d, ds, ds.gbm, &newImp))
    return false;

  // retire old now (fence already waited by caller)
  if (ds.importedReady)
    destroy_imported(d, ds.device, ds.imported);

  ds.imported = newImp;
  ds.importedReady = true;
  ds.importedLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ds.gbm = cur;

  update_overlay_descriptor(d, ds);
  return true;
}

static bool ensure_swapchain_images(const vkroots::VkDeviceDispatch* d,
                                    DeviceState& ds,
                                    VkSwapchainKHR sc)
{
  auto it = ds.swapchains.find(sc);
  if (it == ds.swapchains.end()) return false;

  SwapchainState& ss = it->second;

  uint32_t count = 0;
  VkResult r = d->GetSwapchainImagesKHR(ds.device, sc, &count, nullptr);
  if (r != VK_SUCCESS || count == 0) return false;

  if (ss.imagesInitialized && ss.images.size() == count && ss.views.size() == count)
    return true;

  // destroy old views + fb
  for (VkImageView v : ss.views) {
    if (!v) continue;
    auto fbIt = ds.overlayFbByView.find(v);
    if (fbIt != ds.overlayFbByView.end()) {
      d->DestroyFramebuffer(ds.device, fbIt->second, nullptr);
      ds.overlayFbByView.erase(fbIt);
    }
    d->DestroyImageView(ds.device, v, nullptr);
  }

  ss.images.assign(count, VK_NULL_HANDLE);
  ss.views.assign(count, VK_NULL_HANDLE);

  r = d->GetSwapchainImagesKHR(ds.device, sc, &count, ss.images.data());
  if (r != VK_SUCCESS) {
    ss.images.clear();
    ss.views.clear();
    ss.imagesInitialized = false;
    return false;
  }

  for (uint32_t i = 0; i < count; i++) {
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = ss.images[i];
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ss.format;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;

    if (d->CreateImageView(ds.device, &vci, nullptr, &ss.views[i]) != VK_SUCCESS) {
      ss.views[i] = VK_NULL_HANDLE;
      ss.imagesInitialized = false;
      return false;
    }
  }

  ss.imagesInitialized = true;
  return true;
}

static void destroy_swapchain_cached(const vkroots::VkDeviceDispatch* d,
                                     DeviceState& ds,
                                     VkSwapchainKHR sc)
{
  auto it = ds.swapchains.find(sc);
  if (it == ds.swapchains.end()) return;

  for (VkImageView v : it->second.views) {
    if (!v) continue;
    auto fbIt = ds.overlayFbByView.find(v);
    if (fbIt != ds.overlayFbByView.end()) {
      d->DestroyFramebuffer(ds.device, fbIt->second, nullptr);
      ds.overlayFbByView.erase(fbIt);
    }
    d->DestroyImageView(ds.device, v, nullptr);
  }
  ds.swapchains.erase(it);
}

// ----------------------------
// Barriers
// ----------------------------

static void barrier_image(const vkroots::VkDeviceDispatch* d, VkCommandBuffer cmd,
                          VkImage img, VkImageLayout oldL, VkImageLayout newL,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = oldL;
  b.newLayout = newL;
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b.subresourceRange.levelCount = 1;
  b.subresourceRange.layerCount = 1;

  d->CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// ----------------------------
// Overlay common objects
// ----------------------------

static VkShaderModule make_shader(const vkroots::VkDeviceDispatch* d,
                                  VkDevice dev,
                                  const uint32_t* code,
                                  size_t codeSizeBytes)
{
  VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  sci.codeSize = codeSizeBytes;
  sci.pCode = code;

  VkShaderModule m = VK_NULL_HANDLE;
  if (d->CreateShaderModule(dev, &sci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
  return m;
}

static bool ensure_overlay_common(const vkroots::VkDeviceDispatch* d, DeviceState& ds)
{
  if (ds.overlaySampler != VK_NULL_HANDLE) return true;

  // sampler
  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (d->CreateSampler(ds.device, &sci, nullptr, &ds.overlaySampler) != VK_SUCCESS) return false;

  // descriptor set layout: combined image sampler at binding 0
  VkDescriptorSetLayoutBinding b{};
  b.binding = 0;
  b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  b.descriptorCount = 1;
  b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dlci.bindingCount = 1;
  dlci.pBindings = &b;
  if (d->CreateDescriptorSetLayout(ds.device, &dlci, nullptr, &ds.overlayDSL) != VK_SUCCESS) return false;

  VkPushConstantRange pcr{};
  pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pcr.offset = 0;
  pcr.size = sizeof(float) * 6; // dstExtent(2) + srcExtent(2) + offset(2)

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &ds.overlayDSL;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;

  d->CreatePipelineLayout(ds.device, &plci, nullptr, &ds.overlayPL);

  // descriptor pool + set
  VkDescriptorPoolSize ps{};
  ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  ps.descriptorCount = 1;

  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &ps;
  if (d->CreateDescriptorPool(ds.device, &dpci, nullptr, &ds.overlayDP) != VK_SUCCESS) return false;

  VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = ds.overlayDP;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &ds.overlayDSL;
  if (d->AllocateDescriptorSets(ds.device, &dsai, &ds.overlayDS) != VK_SUCCESS) return false;

  // shaders
  ds.overlayVS = make_shader(d, ds.device, overlay_vert_spv, sizeof(overlay_vert_spv));
  ds.overlayFS = make_shader(d, ds.device, overlay_frag_spv, sizeof(overlay_frag_spv));
  if (ds.overlayVS == VK_NULL_HANDLE || ds.overlayFS == VK_NULL_HANDLE) return false;

  // present chaining semaphore
  VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  if (d->CreateSemaphore(ds.device, &semci, nullptr, &ds.overlayDone) != VK_SUCCESS) return false;

  return true;
}

static VkRenderPass ensure_overlay_renderpass_for_format(const vkroots::VkDeviceDispatch* d,
                                                         DeviceState& ds,
                                                         VkFormat fmt)
{
  auto it = ds.overlayRpByFormat.find(fmt);
  if (it != ds.overlayRpByFormat.end())
    return it->second;

  VkAttachmentDescription att{};
  att.format = fmt;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // overlay
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att.finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sub;
  rpci.dependencyCount = 1;
  rpci.pDependencies = &dep;

  VkRenderPass rp = VK_NULL_HANDLE;
  if (d->CreateRenderPass(ds.device, &rpci, nullptr, &rp) != VK_SUCCESS)
    return VK_NULL_HANDLE;

  ds.overlayRpByFormat[fmt] = rp;
  return rp;
}

static VkFramebuffer ensure_overlay_framebuffer_for_view(const vkroots::VkDeviceDispatch* d,
                                                         DeviceState& ds,
                                                         VkRenderPass rp,
                                                         VkImageView view,
                                                         VkExtent2D extent)
{
  auto it = ds.overlayFbByView.find(view);
  if (it != ds.overlayFbByView.end())
    return it->second;

  VkImageView atts[] = { view };

  VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbci.renderPass = rp;
  fbci.attachmentCount = 1;
  fbci.pAttachments = atts;
  fbci.width = extent.width;
  fbci.height = extent.height;
  fbci.layers = 1;

  VkFramebuffer fb = VK_NULL_HANDLE;
  if (d->CreateFramebuffer(ds.device, &fbci, nullptr, &fb) != VK_SUCCESS)
    return VK_NULL_HANDLE;

  ds.overlayFbByView[view] = fb;
  return fb;
}

static VkPipeline ensure_overlay_pipeline_for_format_v10(const vkroots::VkDeviceDispatch* d,
                                                         DeviceState& ds,
                                                         VkFormat swapFmt,
                                                         VkRenderPass rp)
{
  auto it = ds.overlayPipeByFormat.find(swapFmt);
  if (it != ds.overlayPipeByFormat.end())
    return it->second;

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = ds.overlayVS;
  stages[0].pName = "main";

  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = ds.overlayFS;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount  = 1;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState cba{};
  cba.blendEnable = VK_TRUE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  cba.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT |
    VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;

  VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

  VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gp.stageCount = 2;
  gp.pStages = stages;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vp;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dyn;
  gp.layout = ds.overlayPL;
  gp.renderPass = rp;
  gp.subpass = 0;

  VkPipeline pipe = VK_NULL_HANDLE;
  if (d->CreateGraphicsPipelines(ds.device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe) != VK_SUCCESS)
    return VK_NULL_HANDLE;

  ds.overlayPipeByFormat[swapFmt] = pipe;
  return pipe;
}

static bool record_overlay_draw_v10(const vkroots::VkDeviceDispatch* d,
                                    DeviceState& ds,
                                    VkCommandBuffer cmd,
                                    VkImage swapImg,
                                    VkExtent2D swapExtent,
                                    VkRenderPass rp,
                                    VkFramebuffer fb,
                                    VkPipeline pipe)
{
  d->ResetCommandBuffer(cmd, 0);

  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (d->BeginCommandBuffer(cmd, &bi) != VK_SUCCESS) return false;

  // imported -> SHADER_READ_ONLY
  barrier_image(d, cmd, ds.imported.image,
                ds.importedLayout,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                0, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  ds.importedLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // swapchain PRESENT -> COLOR_ATTACHMENT
  barrier_image(d, cmd, swapImg,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = rp;
  rbi.framebuffer = fb;
  rbi.renderArea.offset = {0, 0};
  rbi.renderArea.extent = swapExtent;
  rbi.clearValueCount = 0;
  rbi.pClearValues = nullptr;

  d->CmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vp{};
  vp.x = 0.0f;
  vp.y = 0.0f;
  vp.width  = (float)swapExtent.width;
  vp.height = (float)swapExtent.height;
  vp.minDepth = 0.0f;
  vp.maxDepth = 1.0f;

  VkRect2D sc{};
  sc.offset = {0, 0};
  sc.extent = swapExtent;

  d->CmdSetViewport(cmd, 0, 1, &vp);
  d->CmdSetScissor(cmd, 0, 1, &sc);

  d->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  d->CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           ds.overlayPL, 0, 1, &ds.overlayDS, 0, nullptr);

  struct PC { float dstW, dstH, srcW, srcH, offX, offY; } pc;
  pc.dstW = (float)swapExtent.width;
  pc.dstH = (float)swapExtent.height;
  pc.srcW = (float)ds.gbm.width;
  pc.srcH = (float)ds.gbm.height;
  pc.offX = 0.0f;   // top-left x
  pc.offY = 0.0f;   // top-left y

  d->CmdPushConstants(cmd, ds.overlayPL, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
  d->CmdDraw(cmd, 3, 1, 0, 0);

  d->CmdEndRenderPass(cmd);

  // back to PRESENT
  barrier_image(d, cmd, swapImg,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  return d->EndCommandBuffer(cmd) == VK_SUCCESS;
}

// ----------------------------
// Swapchain hooks
// ----------------------------

static void OnCreateSwapchain(VkDevice device, VkSwapchainKHR sc, const VkSwapchainCreateInfoKHR* ci) {
  DeviceState* ds = get_ds(device);
  if (!ds) return;

  SwapchainState ss;
  ss.format = ci ? ci->imageFormat : VK_FORMAT_UNDEFINED;
  ss.extent = ci ? ci->imageExtent : VkExtent2D{0, 0};
  ss.imagesInitialized = false;

  ds->swapchains[sc] = ss;

  logf("[mylayer] SwapchainState created sc=%p fmt=%d extent=%ux%u\n",
       (void*)sc, (int)ss.format, ss.extent.width, ss.extent.height);
}

class VkInstanceOverrides {
public:
  static VkResult CreateDevice(
    const vkroots::VkInstanceDispatch* dispatch,
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
  {
    // Add only the external memory extensions needed for dma-buf import.
    std::vector<const char*> exts;
    exts.reserve((pCreateInfo ? pCreateInfo->enabledExtensionCount : 0) + 8);

    if (pCreateInfo) {
      for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
        exts.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
    }

    auto add = [&](const char* n) {
      for (auto* e : exts) if (e && std::strcmp(e, n) == 0) return;
      exts.push_back(n);
    };

    add("VK_KHR_external_memory_fd");
    add("VK_EXT_external_memory_dma_buf");
    add("VK_EXT_image_drm_format_modifier");

    VkDeviceCreateInfo ci = *pCreateInfo;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();

    VkResult r = dispatch->CreateDevice(physicalDevice, &ci, pAllocator, pDevice);
    if (r != VK_SUCCESS) return r;

    // store device state
    auto ds = std::make_unique<DeviceState>();
    ds->device = *pDevice;
    ds->phys = physicalDevice;
    ds->gbm = client.gbm;

    std::lock_guard<std::mutex> lock(g_mtx);
    g_devices[*pDevice] = std::move(ds);

    return r;
  }
};

class VkDeviceOverrides {
public:
  static VkResult CreateSwapchainKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain)
  {
    VkResult r = pDispatch->CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (r == VK_SUCCESS) {
      OnCreateSwapchain(device, *pSwapchain, pCreateInfo);
    }
    return r;
  }

  static void DestroySwapchainKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator)
  {
    DeviceState* ds = get_ds(device);
    if (ds) destroy_swapchain_cached(pDispatch, *ds, swapchain);
    pDispatch->DestroySwapchainKHR(device, swapchain, pAllocator);
  }

  static void GetDeviceQueue(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue)
  {
    pDispatch->GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    if (!pQueue || *pQueue == VK_NULL_HANDLE) return;

    DeviceState* ds = get_or_create_ds(device);
    if (!ds) return;

    {
      std::lock_guard<std::mutex> lock(g_qmtx);
      g_queue_to_entry[*pQueue] = { ds, queueFamilyIndex };
    }

    logf("[mylayer] map queue=%p -> dev=%p fam=%u\n",
         (void*)*pQueue, (void*)device, queueFamilyIndex);
  }

  static VkResult QueuePresentKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo)
  {
    static uint64_t last_present_time;
    static uint64_t n_frames = 0;
    uint64_t now = os_time_get_nano(); /* ns */
    uint64_t frametime_ns = now - last_present_time;
    last_present_time = now;
    float value = frametime_ns / 1000000.f;
    static uint64_t last_60hz_time = 0;
    const uint64_t PERIOD_NS = 1000000000ULL / 165;
    if (last_60hz_time == 0) {
      last_60hz_time = now;
    }

    client.add_to_queue(value);
    if (now - last_60hz_time >= PERIOD_NS) {
      client.drain_queue();
      last_60hz_time += PERIOD_NS;
    }
    // sockclient.push(value);
    n_frames++;
    if (!pPresentInfo || pPresentInfo->swapchainCount == 0)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    QueueMapEntry qe{};
    {
      std::lock_guard<std::mutex> lock(g_qmtx);
      auto it = g_queue_to_entry.find(queue);
      if (it != g_queue_to_entry.end()) qe = it->second;
    }
    DeviceState* ds = qe.ds;
    uint32_t fam = qe.family;

    if (!ds)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    CmdObjects* co = ensure_cmd_objects_for_family(pDispatch, *ds, fam);
    if (!co)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    if (!ensure_overlay_common(pDispatch, *ds))
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    VkSwapchainKHR sc = pPresentInfo->pSwapchains[0];
    uint32_t idx = pPresentInfo->pImageIndices[0];

    auto it = ds->swapchains.find(sc);
    if (it == ds->swapchains.end())
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    if (!ensure_swapchain_images(pDispatch, *ds, sc))
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    SwapchainState& ss = it->second;
    if (!ss.imagesInitialized || idx >= ss.images.size() || idx >= ss.views.size())
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    if (ss.views[idx] == VK_NULL_HANDLE)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    update_overlay_descriptor(pDispatch, *ds);

    VkRenderPass rp = ensure_overlay_renderpass_for_format(pDispatch, *ds, ss.format);
    if (rp == VK_NULL_HANDLE)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    VkFramebuffer fb = ensure_overlay_framebuffer_for_view(pDispatch, *ds, rp, ss.views[idx], ss.extent);
    if (fb == VK_NULL_HANDLE)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    VkPipeline pipe = ensure_overlay_pipeline_for_format_v10(pDispatch, *ds, ss.format, rp);
    if (pipe == VK_NULL_HANDLE)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    // Wait for our previous overlay submit on this queue family
    VkResult wr = pDispatch->WaitForFences(ds->device, 1, &co->fence, VK_TRUE, UINT64_MAX);
    if (wr != VK_SUCCESS)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    if (!refresh_imported_if_needed(pDispatch, *ds)) {

      return pDispatch->QueuePresentKHR(queue, pPresentInfo);
    }


    if (!ds->importedReady)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    pDispatch->ResetFences(ds->device, 1, &co->fence);

    if (!record_overlay_draw_v10(pDispatch, *ds, co->cmd,
                                ss.images[idx], ss.extent,
                                rp, fb, pipe)) {
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);
    }

    // Submit overlay after waiting on app semaphores
    std::vector<VkPipelineStageFlags> waitStages(
      pPresentInfo->waitSemaphoreCount,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
    si.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
    si.pWaitDstStageMask = waitStages.data();
    si.commandBufferCount = 1;
    si.pCommandBuffers = &co->cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &ds->overlayDone;

    VkResult sr = pDispatch->QueueSubmit(queue, 1, &si, co->fence);
    if (sr != VK_SUCCESS)
      return pDispatch->QueuePresentKHR(queue, pPresentInfo);

    // Present but wait on our overlay completion semaphore
    VkPresentInfoKHR pi = *pPresentInfo;
    VkSemaphore waits[] = { ds->overlayDone };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = waits;
    return pDispatch->QueuePresentKHR(queue, &pi);
  }

  static void DestroyDevice(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    const VkAllocationCallbacks* pAllocator)
  {
    DeviceState* ds = get_ds(device);
    if (ds) {
      // swapchain cached views/fbs destroyed via DestroySwapchainKHR normally, but do best-effort
      for (auto& kv : ds->overlayFbByView)
        pDispatch->DestroyFramebuffer(device, kv.second, nullptr);
      ds->overlayFbByView.clear();

      for (auto& kv : ds->overlayPipeByFormat)
        pDispatch->DestroyPipeline(device, kv.second, nullptr);
      ds->overlayPipeByFormat.clear();

      for (auto& kv : ds->overlayRpByFormat)
        pDispatch->DestroyRenderPass(device, kv.second, nullptr);
      ds->overlayRpByFormat.clear();

      if (ds->overlayDone) pDispatch->DestroySemaphore(device, ds->overlayDone, nullptr);

      if (ds->overlayVS) pDispatch->DestroyShaderModule(device, ds->overlayVS, nullptr);
      if (ds->overlayFS) pDispatch->DestroyShaderModule(device, ds->overlayFS, nullptr);

      if (ds->overlayDP) pDispatch->DestroyDescriptorPool(device, ds->overlayDP, nullptr);
      if (ds->overlayPL) pDispatch->DestroyPipelineLayout(device, ds->overlayPL, nullptr);
      if (ds->overlayDSL) pDispatch->DestroyDescriptorSetLayout(device, ds->overlayDSL, nullptr);
      if (ds->overlaySampler) pDispatch->DestroySampler(device, ds->overlaySampler, nullptr);

      if (ds->imported.view) pDispatch->DestroyImageView(device, ds->imported.view, nullptr);
      if (ds->imported.image) pDispatch->DestroyImage(device, ds->imported.image, nullptr);
      if (ds->imported.memory) pDispatch->FreeMemory(device, ds->imported.memory, nullptr);

      for (auto& kv : ds->cmdByFamily) {
        auto& co = kv.second;
        if (co.fence) pDispatch->DestroyFence(device, co.fence, nullptr);
        if (co.pool) {
          if (co.cmd) pDispatch->FreeCommandBuffers(device, co.pool, 1, &co.cmd);
          pDispatch->DestroyCommandPool(device, co.pool, nullptr);
        }
      }
      ds->cmdByFamily.clear();
    }

    erase_ds(device);
    pDispatch->DestroyDevice(device, pAllocator);
  }
};

} // namespace MyLayer

VKROOTS_DEFINE_LAYER_INTERFACES(
  MyLayer::VkInstanceOverrides,
  vkroots::NoOverrides,
  MyLayer::VkDeviceOverrides
);
