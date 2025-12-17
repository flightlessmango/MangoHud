import copy
import re
import xml.etree.ElementTree as et

def get_api_list(s):
    apis = []
    for a in s.split(','):
        if a == 'disabled':
            continue
        assert a in ('vulkan', 'vulkansc')
        apis.append(a)
    return apis

class Extension:
    def __init__(self, name, number, ext_version):
        self.name = name
        self.type = None
        self.number = number
        self.platform = None
        self.provisional = False
        self.ext_version = int(ext_version)
        self.supported = []

    def from_xml(ext_elem):
        name = ext_elem.attrib['name']
        number = int(ext_elem.attrib['number'])
        supported = get_api_list(ext_elem.attrib['supported'])
        if name == 'VK_ANDROID_native_buffer':
            assert not supported
            supported = ['vulkan']

        if not supported:
            return Extension(name, number, 0)

        version = None
        for enum_elem in ext_elem.findall('.require/enum'):
            if enum_elem.attrib['name'].endswith('_SPEC_VERSION'):
                # Skip alias SPEC_VERSIONs
                if 'value' in enum_elem.attrib:
                    assert version is None
                    version = int(enum_elem.attrib['value'])

        assert version is not None
        ext = Extension(name, number, version)
        ext.type = ext_elem.attrib['type']
        ext.platform = ext_elem.attrib.get('platform', None)
        ext.provisional = ext_elem.attrib.get('provisional', False)
        ext.supported = supported

        return ext

    def c_android_condition(self):
        # if it's an EXT or vendor extension, it's allowed
        if not self.name.startswith(ANDROID_EXTENSION_WHITELIST_PREFIXES):
            return 'true'

        allowed_version = ALLOWED_ANDROID_VERSION.get(self.name, None)
        if allowed_version is None:
            return 'false'

        return 'ANDROID_API_LEVEL >= %d' % (allowed_version)

class ApiVersion:
    def __init__(self, version):
        self.version = version

class VkVersion:
    def __init__(self, string):
        split = string.split('.')
        self.major = int(split[0])
        self.minor = int(split[1])
        if len(split) > 2:
            assert len(split) == 3
            self.patch = int(split[2])
        else:
            self.patch = None

        # Sanity check.  The range bits are required by the definition of the
        # VK_MAKE_VERSION macro
        assert self.major < 1024 and self.minor < 1024
        assert self.patch is None or self.patch < 4096
        assert str(self) == string

    def __str__(self):
        ver_list = [str(self.major), str(self.minor)]
        if self.patch is not None:
            ver_list.append(str(self.patch))
        return '.'.join(ver_list)

    def c_vk_version(self):
        ver_list = [str(self.major), str(self.minor), str(self.patch or 0)]
        return 'VK_MAKE_VERSION(' + ', '.join(ver_list) + ')'

    def __int_ver(self):
        # This is just an expansion of VK_VERSION
        return (self.major << 22) | (self.minor << 12) | (self.patch or 0)

    def __gt__(self, other):
        # If only one of them has a patch version, "ignore" it by making
        # other's patch version match self.
        if (self.patch is None) != (other.patch is None):
            other = copy.copy(other)
            other.patch = self.patch

        return self.__int_ver() > other.__int_ver()

    def __le__(self, other):
        return not self.__gt__(other)

# Sort the extension list the way we expect: KHR, then EXT, then vendors
# alphabetically. For digits, read them as a whole number sort that.
# eg.: VK_KHR_8bit_storage < VK_KHR_16bit_storage < VK_EXT_acquire_xlib_display
def extension_order(ext):
    order = []
    for substring in re.split('(KHR|EXT|[0-9]+)', ext.name):
        if substring == 'KHR':
            order.append(1)
        if substring == 'EXT':
            order.append(2)
        elif substring.isdigit():
            order.append(int(substring))
        else:
            order.append(substring)
    return order

def get_all_exts_from_xml(xml, api='vulkan'):
    """ Get a list of all Vulkan extensions. """

    xml = et.parse(xml)

    extensions = []
    for ext_elem in xml.findall('.extensions/extension'):
        ext = Extension.from_xml(ext_elem)
        if api in ext.supported:
            extensions.append(ext)

    return sorted(extensions, key=extension_order)

def init_exts_from_xml(xml, extensions, platform_defines):
    """ Walk the Vulkan XML and fill out extra extension information. """

    xml = et.parse(xml)

    ext_name_map = {}
    for ext in extensions:
        ext_name_map[ext.name] = ext

    # KHR_display is missing from the list.
    platform_defines.append('VK_USE_PLATFORM_DISPLAY_KHR')
    for platform in xml.findall('./platforms/platform'):
        platform_defines.append(platform.attrib['protect'])

    for ext_elem in xml.findall('.extensions/extension'):
        ext_name = ext_elem.attrib['name']
        if ext_name not in ext_name_map:
            continue

        ext = ext_name_map[ext_name]
        ext.type = ext_elem.attrib['type']

class Requirements:
    def __init__(self, core_version=None):
        self.core_version = core_version
        self.extensions = []
        self.guard = None

    def add_extension(self, ext):
        for e in self.extensions:
            if e == ext:
                return;
            assert e.name != ext.name

        self.extensions.append(ext)

def filter_api(elem, api):
    if 'api' not in elem.attrib:
        return True

    return api in elem.attrib['api'].split(',')

def get_alias(aliases, name):
    if name in aliases:
        # in case the spec registry adds an alias chain later
        return get_alias(aliases, aliases[name])
    return name

def get_all_required(xml, thing, api, beta):
    things = {}
    aliases = {}
    for struct in xml.findall('./types/type[@category="struct"][@alias]'):
        if not filter_api(struct, api):
            continue

        name = struct.attrib['name']
        alias = struct.attrib['alias']
        aliases[name] = alias

    for feature in xml.findall('./feature'):
        if not filter_api(feature, api):
            continue

        version = VkVersion(feature.attrib['number'])
        for t in feature.findall('./require/' + thing):
            name = t.attrib['name']
            if name in things:
                assert things[name].core_version <= version
            else:
                things[name] = Requirements(core_version=version)

    for extension in xml.findall('.extensions/extension'):
        ext = Extension.from_xml(extension)
        if api not in ext.supported:
            continue

        if beta != 'true' and ext.provisional:
            continue

        for require in extension.findall('./require'):
            if not filter_api(require, api):
                continue

            for t in require.findall('./' + thing):
                name = get_alias(aliases, t.attrib['name'])
                r = things.setdefault(name, Requirements())
                r.add_extension(ext)

    platform_defines = {}
    for platform in xml.findall('./platforms/platform'):
        name = platform.attrib['name']
        define = platform.attrib['protect']
        platform_defines[name] = define

    for req in things.values():
        if req.core_version is not None:
            continue

        for ext in req.extensions:
            if ext.platform in platform_defines:
                req.guard = platform_defines[ext.platform]
                break

    return things

# Mapping between extension name and the android version in which the extension
# was whitelisted in Android CTS's dEQP-VK.info.device_extensions and
# dEQP-VK.api.info.android.no_unknown_extensions, excluding those blocked by
# android.graphics.cts.VulkanFeaturesTest#testVulkanBlockedExtensions.
ALLOWED_ANDROID_VERSION = {
    # checkInstanceExtensions on oreo-cts-release
    "VK_KHR_surface": 26,
    "VK_KHR_display": 26,
    "VK_KHR_android_surface": 26,
    "VK_KHR_mir_surface": 26,
    "VK_KHR_wayland_surface": 26,
    "VK_KHR_win32_surface": 26,
    "VK_KHR_xcb_surface": 26,
    "VK_KHR_xlib_surface": 26,
    "VK_KHR_get_physical_device_properties2": 26,
    "VK_KHR_get_surface_capabilities2": 26,
    "VK_KHR_external_memory_capabilities": 26,
    "VK_KHR_external_semaphore_capabilities": 26,
    "VK_KHR_external_fence_capabilities": 26,
    # on pie-cts-release
    "VK_KHR_device_group_creation": 28,
    "VK_KHR_get_display_properties2": 28,
    # on android10-tests-release
    "VK_KHR_surface_protected_capabilities": 29,
    # on android13-tests-release
    "VK_KHR_portability_enumeration": 33,

    # checkDeviceExtensions on oreo-cts-release
    "VK_KHR_swapchain": 26,
    "VK_KHR_display_swapchain": 26,
    "VK_KHR_sampler_mirror_clamp_to_edge": 26,
    "VK_KHR_shader_draw_parameters": 26,
    "VK_KHR_maintenance1": 26,
    "VK_KHR_push_descriptor": 26,
    "VK_KHR_descriptor_update_template": 26,
    "VK_KHR_incremental_present": 26,
    "VK_KHR_shared_presentable_image": 26,
    "VK_KHR_storage_buffer_storage_class": 26,
    "VK_KHR_16bit_storage": 26,
    "VK_KHR_get_memory_requirements2": 26,
    "VK_KHR_external_memory": 26,
    "VK_KHR_external_memory_fd": 26,
    "VK_KHR_external_memory_win32": 26,
    "VK_KHR_external_semaphore": 26,
    "VK_KHR_external_semaphore_fd": 26,
    "VK_KHR_external_semaphore_win32": 26,
    "VK_KHR_external_fence": 26,
    "VK_KHR_external_fence_fd": 26,
    "VK_KHR_external_fence_win32": 26,
    "VK_KHR_win32_keyed_mutex": 26,
    "VK_KHR_dedicated_allocation": 26,
    "VK_KHR_variable_pointers": 26,
    "VK_KHR_relaxed_block_layout": 26,
    "VK_KHR_bind_memory2": 26,
    "VK_KHR_maintenance2": 26,
    "VK_KHR_image_format_list": 26,
    "VK_KHR_sampler_ycbcr_conversion": 26,
    # on oreo-mr1-cts-release
    "VK_KHR_draw_indirect_count": 27,
    # on pie-cts-release
    "VK_KHR_device_group": 28,
    "VK_KHR_multiview": 28,
    "VK_KHR_maintenance3": 28,
    "VK_KHR_create_renderpass2": 28,
    "VK_KHR_driver_properties": 28,
    # on android10-tests-release
    "VK_KHR_shader_float_controls": 29,
    "VK_KHR_shader_float16_int8": 29,
    "VK_KHR_8bit_storage": 29,
    "VK_KHR_depth_stencil_resolve": 29,
    "VK_KHR_swapchain_mutable_format": 29,
    "VK_KHR_shader_atomic_int64": 29,
    "VK_KHR_vulkan_memory_model": 29,
    "VK_KHR_swapchain_mutable_format": 29,
    "VK_KHR_uniform_buffer_standard_layout": 29,
    # on android11-tests-release
    "VK_KHR_imageless_framebuffer": 30,
    "VK_KHR_shader_subgroup_extended_types": 30,
    "VK_KHR_buffer_device_address": 30,
    "VK_KHR_separate_depth_stencil_layouts": 30,
    "VK_KHR_timeline_semaphore": 30,
    "VK_KHR_spirv_1_4": 30,
    "VK_KHR_pipeline_executable_properties": 30,
    "VK_KHR_shader_clock": 30,
    # blocked by testVulkanBlockedExtensions
    # "VK_KHR_performance_query": 30,
    "VK_KHR_shader_non_semantic_info": 30,
    "VK_KHR_copy_commands2": 30,
    # on android12-tests-release
    "VK_KHR_shader_terminate_invocation": 31,
    "VK_KHR_ray_tracing_pipeline": 31,
    "VK_KHR_ray_query": 31,
    "VK_KHR_acceleration_structure": 31,
    "VK_KHR_pipeline_library": 31,
    "VK_KHR_deferred_host_operations": 31,
    "VK_KHR_fragment_shading_rate": 31,
    "VK_KHR_zero_initialize_workgroup_memory": 31,
    "VK_KHR_workgroup_memory_explicit_layout": 31,
    "VK_KHR_synchronization2": 31,
    "VK_KHR_shader_integer_dot_product": 31,
    # on android13-tests-release
    "VK_KHR_dynamic_rendering": 33,
    "VK_KHR_format_feature_flags2": 33,
    "VK_KHR_global_priority": 33,
    "VK_KHR_maintenance4": 33,
    "VK_KHR_portability_subset": 33,
    "VK_KHR_present_id": 33,
    "VK_KHR_present_wait": 33,
    "VK_KHR_shader_subgroup_uniform_control_flow": 33,
    # on android14-tests-release
    "VK_KHR_fragment_shader_barycentric": 34,
    "VK_KHR_ray_tracing_maintenance1": 34,
    # blocked by testVulkanBlockedExtensions
    # "VK_KHR_video_decode_h264": 34,
    # "VK_KHR_video_decode_h265": 34,
    # "VK_KHR_video_decode_queue": 34,
    # "VK_KHR_video_queue": 34,
    # on android15-tests-release
    "VK_KHR_calibrated_timestamps": 35,
    "VK_KHR_cooperative_matrix": 35,
    "VK_KHR_dynamic_rendering_local_read": 35,
    "VK_KHR_index_type_uint8": 35,
    "VK_KHR_line_rasterization": 35,
    "VK_KHR_load_store_op_none": 35,
    "VK_KHR_maintenance5": 35,
    "VK_KHR_maintenance6": 35,
    "VK_KHR_map_memory2": 35,
    "VK_KHR_ray_tracing_position_fetch": 35,
    "VK_KHR_shader_expect_assume": 35,
    "VK_KHR_shader_float_controls2": 35,
    "VK_KHR_shader_maximal_reconvergence": 35,
    "VK_KHR_shader_quad_control": 35,
    "VK_KHR_shader_subgroup_rotate": 35,
    "VK_KHR_vertex_attribute_divisor": 35,
    # blocked by testVulkanBlockedExtensions
    # "VK_KHR_video_decode_av1": 35,
    # "VK_KHR_video_encode_h264": 35,
    # "VK_KHR_video_encode_h265": 35,
    # "VK_KHR_video_encode_queue": 35,
    # "VK_KHR_video_maintenance1": 35,

    # testNoUnknownExtensions on oreo-cts-release
    "VK_GOOGLE_display_timing": 26,
    # on pie-cts-release
    "VK_ANDROID_external_memory_android_hardware_buffer": 28,
    # on android11-tests-release
    "VK_GOOGLE_decorate_string": 30,
    "VK_GOOGLE_hlsl_functionality1": 30,
    # on android13-tests-release
    "VK_GOOGLE_surfaceless_query": 33,
    # on android15-tests-release
    "VK_ANDROID_external_format_resolve": 35,

    # this HAL extension is always allowed and will be filtered out by the
    # loader
    "VK_ANDROID_native_buffer": 26,
}

# Extensions with these prefixes are checked in Android CTS, and thus must be
# whitelisted per the preceding dict.
ANDROID_EXTENSION_WHITELIST_PREFIXES = (
    "VK_KHX",
    "VK_KHR",
    "VK_GOOGLE",
    "VK_ANDROID"
)
