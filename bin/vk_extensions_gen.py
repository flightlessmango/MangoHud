COPYRIGHT = """\
/*
 * Copyright 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
"""

import argparse

from mako.template import Template

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_extensions import get_all_exts_from_xml, init_exts_from_xml

_TEMPLATE_H = Template(COPYRIGHT + """

#ifndef VK_EXTENSIONS_H
#define VK_EXTENSIONS_H

#include <stdbool.h>

<%def name="extension_table(type, extensions)">
#define VK_${type.upper()}_EXTENSION_COUNT ${len(extensions)}

extern const VkExtensionProperties vk_${type}_extensions[];

struct vk_${type}_extension_table {
   union {
      bool extensions[VK_${type.upper()}_EXTENSION_COUNT];
      struct {
%for ext in extensions:
         bool ${ext.name[3:]};
%endfor
      };

      /* Workaround for "error: too many initializers for vk_${type}_extension_table" */
      struct {
%for ext in extensions:
         bool ${ext.name[3:]};
%endfor
      } table;
   };
};
</%def>

${extension_table('instance', instance_extensions)}
${extension_table('device', device_extensions)}

struct vk_physical_device;

#ifdef ANDROID_STRICT
extern const struct vk_instance_extension_table vk_android_allowed_instance_extensions;
extern const struct vk_device_extension_table vk_android_allowed_device_extensions;
#endif

#endif /* VK_EXTENSIONS_H */
""")

_TEMPLATE_C = Template(COPYRIGHT + """
#include "vulkan/vulkan_core.h"

#include "vk_extensions.h"

const VkExtensionProperties vk_instance_extensions[VK_INSTANCE_EXTENSION_COUNT] = {
%for ext in instance_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

const VkExtensionProperties vk_device_extensions[VK_DEVICE_EXTENSION_COUNT] = {
%for ext in device_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

#ifdef ANDROID_STRICT
const struct vk_instance_extension_table vk_android_allowed_instance_extensions = {
%for ext in instance_extensions:
   .${ext.name[3:]} = ${ext.c_android_condition()},
%endfor
};

const struct vk_device_extension_table vk_android_allowed_device_extensions = {
%for ext in device_extensions:
   .${ext.name[3:]} = ${ext.c_android_condition()},
%endfor
};
#endif
""")

def gen_extensions(xml_files, extensions, out_c, out_h):
    platform_defines = []
    for filename in xml_files:
        init_exts_from_xml(filename, extensions, platform_defines)

    for ext in extensions:
        assert ext.type in {'instance', 'device'}

    template_env = {
        'instance_extensions': [e for e in extensions if e.type == 'instance'],
        'device_extensions': [e for e in extensions if e.type == 'device'],
        'platform_defines': platform_defines,
    }

    if out_h:
        with open(out_h, 'w', encoding='utf-8') as f:
            f.write(_TEMPLATE_H.render(**template_env))

    if out_c:
        with open(out_c, 'w', encoding='utf-8') as f:
            f.write(_TEMPLATE_C.render(**template_env))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', help='Output C file.')
    parser.add_argument('--out-h', help='Output H file.')
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True,
                        action='append',
                        dest='xml_files')
    args = parser.parse_args()

    extensions = []
    for filename in args.xml_files:
        extensions += get_all_exts_from_xml(filename)

    gen_extensions(args.xml_files, extensions, args.out_c, args.out_h)

if __name__ == '__main__':
    main()
