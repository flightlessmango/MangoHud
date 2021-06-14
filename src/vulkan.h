#include <string>
#include "mesa/util/os_socket.h"
#include "vk_enum_to_str.h"
#include "notify.h"
#include <vulkan/vk_layer.h>
using namespace std;

struct instance_data {
   struct vk_instance_dispatch_table vtable;
   VkInstance instance;
   struct overlay_params params;
   uint32_t api_version;
   string engineName, engineVersion;
   notify_thread notifier;
   int control_client;
};

struct device_data {
   struct instance_data *instance;

   PFN_vkSetDeviceLoaderData set_device_loader_data;

   struct vk_device_dispatch_table vtable;
   VkPhysicalDevice physical_device;
   VkDevice device;

   VkPhysicalDeviceProperties properties;

   struct queue_data *graphic_queue;

   std::vector<struct queue_data *> queues;
};