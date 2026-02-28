#define VKROOTS_LAYER_IMPLEMENTATION
#include <memory>
#include <unordered_map>

#include "vkroots.h"
#include "mesa/os_time.h"
#include "fps_limiter.h"
#include "layer.h"

std::string pEngineName;
uint32_t renderMinor = 0;
PFN_vkSetDeviceLoaderData g_set_device_loader_data = nullptr;
std::unique_ptr<fpsLimiter> fps_limiter;
std::unique_ptr<presentLimiter> present_limiter;
std::unique_ptr<Layer> layer;

static const uint32_t overlay_vert_spv[] = {
    #include "overlay.vert.spv.h"
};
static const uint32_t overlay_frag_spv[] = {
    #include "overlay.frag.spv.h"
};

static bool ChainHasSType(const void* head, VkStructureType sType) {
    for (auto* it = (const VkBaseInStructure*)head; it; it = it->pNext) {
        if (it->sType == sType) {
            return true;
        }
    }
    return false;
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

        add("VK_KHR_external_memory");
        add("VK_KHR_external_memory_fd");
        add("VK_EXT_external_memory_dma_buf");
        add("VK_KHR_external_semaphore");
        add("VK_KHR_external_semaphore_fd");
        add("VK_EXT_image_drm_format_modifier");
        add("VK_KHR_bind_memory2");
        add("VK_KHR_get_memory_requirements2");
        add("VK_KHR_sampler_ycbcr_conversion");
        add("VK_KHR_image_format_list");
        add("VK_KHR_maintenance1");
        add("VK_KHR_present_id");
        add("VK_KHR_present_wait");

        VkDeviceCreateInfo ci = *pCreateInfo;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

        const VkLayerDeviceCreateInfo* layer_info =
            (const VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

        while (layer_info) {
            if (layer_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                layer_info->function == VK_LOADER_DATA_CALLBACK) {
                g_set_device_loader_data = layer_info->u.pfnSetDeviceLoaderData;
                break;
            }

            layer_info = (const VkLayerDeviceCreateInfo*)layer_info->pNext;
        }

        if (!g_set_device_loader_data) {
            fprintf(stderr, "failed to get device loader data\n");
            fprintf(stderr, "we will get validation errors\n");
        }

        if (!layer) layer = std::make_unique<Layer>();
        layer->loader_data = g_set_device_loader_data;

        VkPhysicalDevicePresentIdFeaturesKHR pid{};
        pid.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        pid.presentId = VK_TRUE;

        VkPhysicalDevicePresentWaitFeaturesKHR pw{};
        pw.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
        pw.presentWait = VK_TRUE;

        void* newPNext = (void*)ci.pNext;

        if (!ChainHasSType(newPNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR)) {
            pw.pNext = newPNext;
            newPNext = &pw;
        }

        if (!ChainHasSType(newPNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR)) {
            pid.pNext = newPNext;
            newPNext = &pid;
        }

        ci.pNext = newPNext;
        return dispatch->CreateDevice(physicalDevice, &ci, pAllocator, pDevice);
    }

    static VkResult CreateInstance(
        PFN_vkCreateInstance            pfnCreateInstance,
        const VkInstanceCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkInstance*                     pInstance)
    {
        const char* engine = "";
        if (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pEngineName)
            engine = pCreateInfo->pApplicationInfo->pEngineName;

        pEngineName = engine;

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

        add("VK_KHR_external_memory_capabilities");
        add("VK_KHR_external_semaphore_capabilities");
        add("VK_EXT_debug_utils");

        VkInstanceCreateInfo ci = {};
        if (pCreateInfo) {
            ci = *pCreateInfo;
        }
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

        return pfnCreateInstance(&ci, pAllocator, pInstance);
    }
};

static const VkPresentIdKHR* GetPresentId(const void* pNext) {
    for (auto* it = (const VkBaseInStructure*)pNext; it; it = it->pNext) {
        if (it->sType == VK_STRUCTURE_TYPE_PRESENT_ID_KHR) {
            return (const VkPresentIdKHR*)it;
        }
    }
    return nullptr;
}

class VkDeviceOverrides {
public:
    static VkResult CreateSwapchainKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        VkResult r = pDispatch->CreateSwapchainKHR(pDispatch->Device, pCreateInfo, pAllocator, pSwapchain);
        if (r != VK_SUCCESS)
            return r;

        uint32_t count = 0;
        r = pDispatch->GetSwapchainImagesKHR(pDispatch->Device, *pSwapchain, &count, nullptr);
        if (r != VK_SUCCESS || count == 0) {
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, pAllocator);
            return (r == VK_SUCCESS) ? VK_ERROR_INITIALIZATION_FAILED : r;
        }

        if (!renderMinor) {
            VkPhysicalDeviceDrmPropertiesEXT drm_props{};
            drm_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
            drm_props.pNext = nullptr;

            VkPhysicalDeviceProperties2KHR props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &drm_props;

            auto fpGetPhysicalDeviceProperties2KHR =
                reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
                    pDispatch->pPhysicalDeviceDispatch->pInstanceDispatch->GetInstanceProcAddr(
                    pDispatch->pPhysicalDeviceDispatch->Instance, "vkGetPhysicalDeviceProperties2KHR"));

            fpGetPhysicalDeviceProperties2KHR(pDispatch->PhysicalDevice, &props2);
            if (drm_props.hasPrimary)
                renderMinor = drm_props.renderMinor;
        }

        if (!layer) layer = std::make_unique<Layer>();
        layer->init_overlay_resources(pCreateInfo, pDispatch, count);
        layer->create_swapchain_data(pSwapchain, pCreateInfo, pDispatch);

        layer->g_vkSetDebugUtilsObjectNameEXT =
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(pDispatch->pPhysicalDeviceDispatch->Instance, "vkSetDebugUtilsObjectNameEXT"));
        return VK_SUCCESS;
    }

    static void DestroySwapchainKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator)
    {
        {
            std::lock_guard lock(layer->swapchain_mtx);
            auto it = layer->swapchains.find(swapchain);
            if (it != layer->swapchains.end())
                layer->swapchains.erase(it);
        }

        pDispatch->DestroySwapchainKHR(pDispatch->Device, swapchain, pAllocator);
    }

    static VkResult QueuePresentKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkQueue queue,
        const VkPresentInfoKHR* pPresentInfo)
    {
        layer->init_cmd(queue);
        uint32_t swapchain_image_count = 0;
        pDispatch->GetSwapchainImagesKHR(pDispatch->Device, pPresentInfo->pSwapchains[0], &swapchain_image_count, nullptr);
        uint32_t imageIndex = pPresentInfo->pImageIndices[0];
        VkPresentInfoKHR pi = *pPresentInfo;

        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        {
            std::lock_guard lock(fps_limiter->q_limiter->present_queues_mtx);
            fps_limiter->q_limiter->present_queues.insert(queue);
        }

        fps_limiter->limit(true);
        layer->ipc->add_to_queue(os_time_get_nano());
        // TODO Probably don't do this every frame
        fps_limiter->set_fps_limit(layer->ipc->fps_limit);

        layer->ipc->start(renderMinor, pEngineName, swapchain_image_count, layer->ipc);
        {
            std::lock_guard lock(layer->overlay_vk->m);
            if (!layer->overlay_vk->draw(pPresentInfo->pSwapchains[0], imageIndex, queue))
                return pDispatch->QueuePresentKHR(queue, pPresentInfo);
        }

        if (!present_limiter)
            present_limiter = std::make_unique<presentLimiter>(pDispatch->WaitForPresentKHR);

        const VkPresentIdKHR* existing_pid = GetPresentId(pi.pNext);

        static thread_local std::vector<uint64_t> tl_ids;
        static thread_local VkPresentIdKHR tl_pid;

        const uint64_t* ids_ptr = nullptr;

        if (existing_pid && existing_pid->pPresentIds && existing_pid->swapchainCount == pi.swapchainCount) {
            ids_ptr = existing_pid->pPresentIds;
        } else {
            tl_ids.resize(pi.swapchainCount);
            present_limiter->on_present(&pi, tl_ids.data());

            tl_pid = {};
            tl_pid.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
            tl_pid.swapchainCount = pi.swapchainCount;
            tl_pid.pPresentIds = tl_ids.data();
            tl_pid.pNext = (void*)pi.pNext;
            pi.pNext = &tl_pid;

            ids_ptr = tl_ids.data();
        }

        VkResult r;

        {
            std::lock_guard lock(layer->overlay_vk->m);
            r = pDispatch->QueuePresentKHR(queue, &pi);
        }

        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            if (ids_ptr)
                present_limiter->on_present_result(&pi, ids_ptr, r);


            if (fps_limiter && fps_limiter->active)
                present_limiter->throttle(pDispatch->Device, pi.pSwapchains[0], 1);
        }

        return r;
    }

    static void GetDeviceQueue(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        uint32_t queueFamilyIndex,
        uint32_t queueIndex,
        VkQueue* pQueue)
    {
        pDispatch->GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        if (!layer) layer = std::make_unique<Layer>();
        {
            std::lock_guard lock(layer->q_family_mtx);
            layer->queue_family.try_emplace(*pQueue, queueFamilyIndex);
        }
    }

    static void GetDeviceQueue2(const vkroots::VkDeviceDispatch* pDispatch,
                                VkDevice device,
                                const VkDeviceQueueInfo2* pQueueInfo,
                                VkQueue* pQueue)
    {
        pDispatch->GetDeviceQueue2(device, pQueueInfo, pQueue);
        if (!layer) layer = std::make_unique<Layer>();
        {
            std::lock_guard lock(layer->q_family_mtx);
            layer->queue_family.try_emplace(*pQueue, pQueueInfo->queueFamilyIndex);
        }
    }

    static void DestroyDevice(const vkroots::VkDeviceDispatch* d, VkDevice device, const VkAllocationCallbacks* pAllocator) {
        d->DeviceWaitIdle(device);
        if (layer) layer.reset();
        fps_limiter.reset();

        d->DestroyDevice(device, pAllocator);
    }

    static VkResult QueueSubmit(const vkroots::VkDeviceDispatch* d, VkQueue queue, uint32_t submitCount,
                                const VkSubmitInfo *pSubmits, VkFence fence)
    {
        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        auto& q_limiter = fps_limiter->q_limiter;
        if (q_limiter->is_present_queue(queue))
            q_limiter->throttle_before_submit(d);
        {
            std::lock_guard lock(layer->overlay_vk->m);
            d->QueueSubmit(queue, submitCount, pSubmits, fence);
        }

        if (q_limiter->is_present_queue(queue)) {
            std::lock_guard lock(layer->overlay_vk->m);
            VkResult r2 = q_limiter->mark_after_submit(d, queue);
            if (r2 != VK_SUCCESS)
                return r2;
        }

        return VK_SUCCESS;
    }

    static VkResult AcquireNextImageKHR(const vkroots::VkDeviceDispatch* pDispatch,
                                        VkDevice device, VkSwapchainKHR swapchain,
                                        uint64_t timeout, VkSemaphore semaphore,
                                        VkFence fence, uint32_t *pImageIndex)
    {
        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        VkResult r = pDispatch->AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
        if (r == VK_SUCCESS)
            fps_limiter->limit(false);

        return r;
    }

    static VkResult AcquireNextImage2KHR(const vkroots::VkDeviceDispatch* pDispatch,
                                         VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex)
    {
        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        VkResult r = pDispatch->AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
        if (r == VK_SUCCESS)
            fps_limiter->limit(false);

        return r;
    }
};

VKROOTS_DEFINE_LAYER_INTERFACES(
  VkInstanceOverrides,
  vkroots::NoOverrides,
  VkDeviceOverrides
);


void Layer::init_overlay_resources(const VkSwapchainCreateInfoKHR* pCreateInfo, const vkroots::VkDeviceDispatch* pDispatch, uint32_t image_count) {
    if (ovl_res)
        return;

    auto d = std::make_shared<const vkroots::VkDeviceDispatch>(*pDispatch);
    ovl_res = std::make_shared<overlay_resources>(d);

    if (!ovl_res->vs) {
        ovl_res->vs = make_shader(d.get(), d->Device, overlay_vert_spv, sizeof(overlay_vert_spv));
        SetName(d->Device, VK_OBJECT_TYPE_SHADER_MODULE, uint64_t(ovl_res->vs), "mangohud_vert_shader");
    }

    if (!ovl_res->fs) {
        ovl_res->fs = make_shader(d.get(), d->Device, overlay_frag_spv, sizeof(overlay_frag_spv));
        SetName(d->Device, VK_OBJECT_TYPE_SHADER_MODULE, uint64_t(ovl_res->fs), "mangohud_frag_shader");
    }

    if (!ovl_res->sampler) {
        VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = VK_FILTER_NEAREST;
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.minLod = 0.0f;
        sci.maxLod = 0.0f;
        sci.maxAnisotropy = 1.0f;
        sci.unnormalizedCoordinates = VK_FALSE;

        VkResult r = d->CreateSampler(d->Device, &sci, nullptr, &ovl_res->sampler);
        if (r != VK_SUCCESS)
            SPDLOG_ERROR("CreateSampler {}", string_VkResult(r));
        SetName(d->Device, VK_OBJECT_TYPE_SAMPLER, uint64_t(ovl_res->sampler), "mangohud_sampler");
    }

    if (!ovl_res->dsl) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dsci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dsci.bindingCount = 1;
        dsci.pBindings = &b;

        VkResult r = d->CreateDescriptorSetLayout(d->Device, &dsci, nullptr, &ovl_res->dsl);
        if (r != VK_SUCCESS)
            SPDLOG_ERROR("CreateDescriptorSetLayout {}", string_VkResult(r));
        SetName(d->Device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, uint64_t(ovl_res->dsl), "mangohud_descriptor_set_layout");
    }

    if (!ovl_res->dp) {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = image_count * 2; // * 2 because we need one per cache as well

        VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dpci.maxSets = image_count * 2;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;

        VkResult r = d->CreateDescriptorPool(d->Device, &dpci, nullptr, &ovl_res->dp);
        if (r != VK_SUCCESS)
            SPDLOG_ERROR("CreateDescriptorPool {}", string_VkResult(r));
        SetName(d->Device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, uint64_t(ovl_res->dp), "mangohud_descriptor_pool");
    }

    if (!ovl_res->pl) {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(OverlayPushConsts);

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &ovl_res->dsl;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;

        VkResult r = d->CreatePipelineLayout(d->Device, &plci, nullptr, &ovl_res->pl);
        if (r != VK_SUCCESS)
            SPDLOG_ERROR("CreatePipelineLayout {}", string_VkResult(r));
        SetName(d->Device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, uint64_t(ovl_res->pl), "mangohud_pipeline_layout");
    }

    ovl_res->cmd_fences.resize(image_count);
    for (size_t i = 0; i < image_count; i++) {
        if (ovl_res->cmd_fences[i] == VK_NULL_HANDLE) {
            VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VkResult r = d->CreateFence(d->Device, &fci, nullptr, &ovl_res->cmd_fences[i]);
            if (r != VK_SUCCESS) {
                d->DestroyFence(d->Device, ovl_res->cmd_fences[i], nullptr);
                ovl_res->cmd_fences[i] = VK_NULL_HANDLE;
                SPDLOG_ERROR("CreateFence {}", string_VkResult(r));
            }
            SetName(d->Device, VK_OBJECT_TYPE_FENCE, (uint64_t)ovl_res->cmd_fences[i],
                    "mangohud_overlay_cmd_fence_%zu", i);
        }
    }

    ovl_res->overlay_done.resize(image_count);
    for (size_t i = 0; i < image_count; ++i) {
        auto& sema = ovl_res->overlay_done[i];
        if (sema == VK_NULL_HANDLE) {
            VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VkResult r = d->CreateSemaphore(d->Device, &si, nullptr, &sema);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("CreateSemaphore {}", string_VkResult(r));

            SetName(d->Device, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)sema,
                    "mangohud_overlay_done_semaphore_%zu", i);
        }
    }
}
