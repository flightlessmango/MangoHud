#include <mutex>
#include <list>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include "overlay.h"
#include <inttypes.h>
#include "mesa/util/macros.h"
#include <vulkan/vk_util.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

VkPhysicalDeviceDriverProperties driverProps = {};

/* Mapped from VkCommandBuffer */
struct queue_data;

/* Mapped from VkQueue */
struct queue_data {
   struct device_data *device;

   VkQueue queue;
   VkQueueFlags flags;
   uint32_t family_index;
};

// single global lock, for simplicity
std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;
std::unordered_map<uint64_t, void *> vk_object_to_data;

#define HKEY(obj) ((uint64_t)(obj))
#define FIND(type, obj) (reinterpret_cast<type *>(find_object_data(HKEY(obj))))

static void *find_object_data(uint64_t obj)
{
   ::scoped_lock lk(global_lock);
   return vk_object_to_data[obj];
}

static void map_object(uint64_t obj, void *data)
{
   ::scoped_lock lk(global_lock);
   vk_object_to_data[obj] = data;
}

static void unmap_object(uint64_t obj)
{
   ::scoped_lock lk(global_lock);
   vk_object_to_data.erase(obj);
}

static VkLayerInstanceCreateInfo *get_instance_chain_info(const VkInstanceCreateInfo *pCreateInfo,
                                                          VkLayerFunction func)
{
   vk_foreach_struct(item, pCreateInfo->pNext) {
      if (item->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
          ((VkLayerInstanceCreateInfo *) item)->function == func)
         return (VkLayerInstanceCreateInfo *) item;
   }
   unreachable("instance chain info not found");
   return NULL;
}

static VkLayerDeviceCreateInfo *get_device_chain_info(const VkDeviceCreateInfo *pCreateInfo,
                                                      VkLayerFunction func)
{
   vk_foreach_struct(item, pCreateInfo->pNext) {
      if (item->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
          ((VkLayerDeviceCreateInfo *) item)->function == func)
         return (VkLayerDeviceCreateInfo *)item;
   }
   unreachable("device chain info not found");
   return NULL;
}

/**/

static struct instance_data *new_instance_data(VkInstance instance)
{
   struct instance_data *data = new instance_data();
   data->instance = instance;
   data->params = {};
   data->params.control = -1;
   map_object(HKEY(data->instance), data);
   return data;
}

static void destroy_instance_data(struct instance_data *data)
{
   unmap_object(HKEY(data->instance));
   delete data;
}

static void instance_data_map_physical_devices(struct instance_data *instance_data,
                                               bool map)
{
   uint32_t physicalDeviceCount = 0;
   instance_data->vtable.EnumeratePhysicalDevices(instance_data->instance,
                                                  &physicalDeviceCount,
                                                  NULL);

   std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
   instance_data->vtable.EnumeratePhysicalDevices(instance_data->instance,
                                                  &physicalDeviceCount,
                                                  physicalDevices.data());

   for (uint32_t i = 0; i < physicalDeviceCount; i++) {
      if (map)
         map_object(HKEY(physicalDevices[i]), instance_data);
      else
         unmap_object(HKEY(physicalDevices[i]));
   }
}

/**/
static struct device_data *new_device_data(VkDevice device, struct instance_data *instance)
{
   struct device_data *data = new device_data();
   data->instance = instance;
   data->device = device;
   map_object(HKEY(data->device), data);
   return data;
}

static void destroy_queue(struct queue_data *data)
{
   unmap_object(HKEY(data->queue));
   delete data;
}

static void device_unmap_queues(struct device_data *data)
{
   for (auto q : data->queues)
      destroy_queue(q);
}

static void destroy_device_data(struct device_data *data)
{
   unmap_object(HKEY(data->device));
   delete data;
}

static VkResult overlay_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
    struct instance_data *instance_data =
        FIND(struct instance_data, physicalDevice);
    VkLayerDeviceCreateInfo *chain_info =
        get_device_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(NULL, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkPhysicalDeviceFeatures device_features = {};
    VkDeviceCreateInfo device_info = *pCreateInfo;

    std::vector<const char*> enabled_extensions(device_info.ppEnabledExtensionNames,
                                                device_info.ppEnabledExtensionNames +
                                                device_info.enabledExtensionCount);

    uint32_t extension_count;
    instance_data->vtable.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    instance_data->vtable.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, available_extensions.data());


    bool can_get_driver_info = instance_data->api_version < VK_API_VERSION_1_1 ? false : true;

    // VK_KHR_driver_properties became core in 1.2
    if (instance_data->api_version < VK_API_VERSION_1_2 && can_get_driver_info) {
        for (auto& extension : available_extensions) {
            if (extension.extensionName == std::string(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
                for (auto& enabled : enabled_extensions) {
                if (enabled == std::string(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
                    goto DONT;
                }
                enabled_extensions.push_back(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
                DONT:
                goto FOUND;
            }
        }
        can_get_driver_info = false;
        FOUND:;
    }

    device_info.enabledExtensionCount = enabled_extensions.size();
    device_info.ppEnabledExtensionNames = enabled_extensions.data();

    if (pCreateInfo->pEnabledFeatures)
        device_features = *(pCreateInfo->pEnabledFeatures);
    device_info.pEnabledFeatures = &device_features;


    VkResult result = fpCreateDevice(physicalDevice, &device_info, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;

    struct device_data *device_data = new_device_data(*pDevice, instance_data);
    device_data->physical_device = physicalDevice;
    vk_load_device_commands(*pDevice, fpGetDeviceProcAddr, &device_data->vtable);

    instance_data->vtable.GetPhysicalDeviceProperties(device_data->physical_device,
                                                        &device_data->properties);

    VkLayerDeviceCreateInfo *load_data_info =
        get_device_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
    device_data->set_device_loader_data = load_data_info->u.pfnSetDeviceLoaderData;

    driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    driverProps.pNext = nullptr;
    if (can_get_driver_info) {
        VkPhysicalDeviceProperties2 deviceProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driverProps};
        instance_data->vtable.GetPhysicalDeviceProperties2(device_data->physical_device, &deviceProps);
    }

    return result;
}

static void overlay_DestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator)
{
    struct device_data *device_data = FIND(struct device_data, device);
    device_unmap_queues(device_data);
    device_data->vtable.DestroyDevice(device, pAllocator);
    destroy_device_data(device_data);
}

static VkResult overlay_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
    VkLayerInstanceCreateInfo *chain_info =
        get_instance_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    std::string engineVersion,engineName;
    enum EngineTypes engine = EngineTypes::UNKNOWN;

    const char* pEngineName = nullptr;
    if (pCreateInfo->pApplicationInfo)
        pEngineName = pCreateInfo->pApplicationInfo->pEngineName;
    if (pEngineName)
        engineName = pEngineName;
    if (engineName == "DXVK" || engineName == "vkd3d") {
        int engineVer = pCreateInfo->pApplicationInfo->engineVersion;
        engineVersion = to_string(VK_VERSION_MAJOR(engineVer)) + "." + to_string(VK_VERSION_MINOR(engineVer)) + "." + to_string(VK_VERSION_PATCH(engineVer));
    }

    if (engineName == "DXVK")
        engine = DXVK;

    else if (engineName == "vkd3d")
        engine = VKD3D;

    else if(engineName == "mesa zink")
        engine = ZINK;

    else if (engineName == "Damavand")
        engine = DAMAVAND;

    else if (engineName == "Feral3D")
        engine = FERAL3D;

    else
        engine = VULKAN;

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance fpCreateInstance =
        (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;


    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    struct instance_data *instance_data = new_instance_data(*pInstance);
    vk_load_instance_commands(instance_data->instance,
                                fpGetInstanceProcAddr,
                                &instance_data->vtable);
    instance_data_map_physical_devices(instance_data, true);

    instance_data->engine = engine;
    instance_data->engineName = engineName;
    instance_data->engineVersion = engineVersion;

    struct stat info;
    string path = "/tmp/mangoapp/";
    string command = "mkdir -p " + path;
    string json_path = path + to_string(getpid()) + ".json"; 
    if( stat(path.c_str(), &info ) != 0 )
        system(command.c_str());
    json j;
    j["engine"] = engine;
    ofstream o(json_path);
    if (!o.fail()){
        o << std::setw(4) << j << std::endl;
    } else{
        fprintf(stderr, "MANGOAPP LAYER: failed to write json\n");
    }
    o.close();

    return result;
}

static void overlay_DestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator)
{
   struct instance_data *instance_data = FIND(struct instance_data, instance);
   instance_data_map_physical_devices(instance_data, false);
   instance_data->vtable.DestroyInstance(instance, pAllocator);
   destroy_instance_data(instance_data);
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL overlay_GetDeviceProcAddr(VkDevice dev,
                                                                             const char *funcName);
extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL overlay_GetInstanceProcAddr(VkInstance instance,
                                                                               const char *funcName);

static const struct {
   const char *name;
   void *ptr;
} name_to_funcptr_map[] = {
   { "vkGetInstanceProcAddr", (void *) overlay_GetInstanceProcAddr },
   { "vkGetDeviceProcAddr", (void *) overlay_GetDeviceProcAddr },
#define ADD_HOOK(fn) { "vk" # fn, (void *) overlay_ ## fn }
#define ADD_ALIAS_HOOK(alias, fn) { "vk" # alias, (void *) overlay_ ## fn }
   ADD_HOOK(CreateDevice),
   ADD_HOOK(DestroyDevice),

   ADD_HOOK(CreateInstance),
   ADD_HOOK(DestroyInstance),
#undef ADD_HOOK
};

static void *find_ptr(const char *name)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(name_to_funcptr_map); i++) {
      if (strcmp(name, name_to_funcptr_map[i].name) == 0)
         return name_to_funcptr_map[i].ptr;
   }

   return NULL;
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL overlay_GetDeviceProcAddr(VkDevice dev,
                                                                             const char *funcName)
{
   void *ptr = find_ptr(funcName);
   if (ptr) return reinterpret_cast<PFN_vkVoidFunction>(ptr);

   if (dev == NULL) return NULL;

   struct device_data *device_data = FIND(struct device_data, dev);
   if (device_data->vtable.GetDeviceProcAddr == NULL) return NULL;
   return device_data->vtable.GetDeviceProcAddr(dev, funcName);
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL overlay_GetInstanceProcAddr(VkInstance instance,
                                                                               const char *funcName)
{
   void *ptr = find_ptr(funcName);
   if (ptr) return reinterpret_cast<PFN_vkVoidFunction>(ptr);

   if (instance == NULL) return NULL;

   struct instance_data *instance_data = FIND(struct instance_data, instance);
   if (instance_data->vtable.GetInstanceProcAddr == NULL) return NULL;
   return instance_data->vtable.GetInstanceProcAddr(instance, funcName);
}
