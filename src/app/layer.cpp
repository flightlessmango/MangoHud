#include <mutex>
#include <list>
#include <fstream>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include "mesa/util/macros.h"
#include "vk_enum_to_str.h"
#include <vulkan/vk_layer.h>
#include <vulkan/vk_util.h>
#include "nlohmann/json.hpp"
#include "engine_types.h"

using namespace std;
using json = nlohmann::json;

// single global lock, for simplicity
std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;
std::unordered_map<uint64_t, void *> vk_object_to_data;

/* Mapped from VkInstace/VkPhysicalDevice */
struct instance_data {
   struct vk_instance_dispatch_table vtable;
   VkInstance instance;
   string engineName, engineVersion;
   enum EngineTypes engine;
};

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

static struct instance_data *new_instance_data(VkInstance instance)
{
   struct instance_data *data = new instance_data();
   data->instance = instance;
   map_object(HKEY(data->instance), data);
   return data;
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

static VkResult overlay_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
    fprintf(stderr, "MANGOAPP LAYER: Init\n");
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

extern "C" VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL overlay_GetInstanceProcAddr(VkInstance instance,
                                                                               const char *funcName);

static const struct {
   const char *name;
   void *ptr;
} name_to_funcptr_map[] = {
   { "vkGetInstanceProcAddr", (void *) overlay_GetInstanceProcAddr },
#define ADD_HOOK(fn) { "vk" # fn, (void *) overlay_ ## fn }
#define ADD_ALIAS_HOOK(alias, fn) { "vk" # alias, (void *) overlay_ ## fn }
   ADD_HOOK(CreateInstance),
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
