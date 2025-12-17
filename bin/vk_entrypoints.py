# Copyright 2020 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sub license, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
# IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import xml.etree.ElementTree as et

from collections import OrderedDict, namedtuple

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_extensions import get_all_required, filter_api

EntrypointParam = namedtuple('EntrypointParam', 'type name decl len')

class EntrypointBase:
    def __init__(self, name):
        assert name.startswith('vk')
        self.name = name[2:]
        self.alias = None
        self.guard = None
        self.entry_table_index = None
        # Extensions which require this entrypoint
        self.core_version = None
        self.extensions = []

    def prefixed_name(self, prefix):
        return prefix + '_' + self.name

class Entrypoint(EntrypointBase):
    def __init__(self, name, return_type, params):
        super(Entrypoint, self).__init__(name)
        self.return_type = return_type
        self.params = params
        self.guard = None
        self.aliases = []
        self.disp_table_index = None

    def is_physical_device_entrypoint(self):
        return self.params[0].type in ('VkPhysicalDevice', )

    def is_device_entrypoint(self):
        return self.params[0].type in ('VkDevice', 'VkCommandBuffer', 'VkQueue')

    def decl_params(self, start=0):
        return ', '.join(p.decl for p in self.params[start:])

    def call_params(self, start=0):
        return ', '.join(p.name for p in self.params[start:])

class EntrypointAlias(EntrypointBase):
    def __init__(self, name, entrypoint):
        super(EntrypointAlias, self).__init__(name)
        self.alias = entrypoint
        entrypoint.aliases.append(self)

    def is_physical_device_entrypoint(self):
        return self.alias.is_physical_device_entrypoint()

    def is_device_entrypoint(self):
        return self.alias.is_device_entrypoint()

    def prefixed_name(self, prefix):
        return self.alias.prefixed_name(prefix)

    @property
    def params(self):
        return self.alias.params

    @property
    def return_type(self):
        return self.alias.return_type

    @property
    def disp_table_index(self):
        return self.alias.disp_table_index

    def decl_params(self):
        return self.alias.decl_params()

    def call_params(self):
        return self.alias.call_params()

def get_entrypoints(doc, api, beta):
    """Extract the entry points from the registry."""
    entrypoints = OrderedDict()

    required = get_all_required(doc, 'command', api, beta)

    for command in doc.findall('./commands/command'):
        if not filter_api(command, api):
            continue

        if 'alias' in command.attrib:
            name = command.attrib['name']
            target = command.attrib['alias']
            e = EntrypointAlias(name, entrypoints[target])
        else:
            name = command.find('./proto/name').text
            ret_type = command.find('./proto/type').text
            params = [EntrypointParam(
                type=p.find('./type').text,
                name=p.find('./name').text,
                decl=''.join(p.itertext()),
                len=p.attrib.get('altlen', p.attrib.get('len', None))
            ) for p in command.findall('./param') if filter_api(p, api)]
            # They really need to be unique
            e = Entrypoint(name, ret_type, params)

        if name not in required:
            continue

        r = required[name]
        e.core_version = r.core_version
        e.extensions = r.extensions
        e.guard = r.guard

        assert name not in entrypoints, name
        entrypoints[name] = e

    return entrypoints.values()

def get_entrypoints_from_xml(xml_files, beta, api='vulkan'):
    entrypoints = []

    for filename in xml_files:
        doc = et.parse(filename)
        entrypoints += get_entrypoints(doc, api, beta)

    return entrypoints
