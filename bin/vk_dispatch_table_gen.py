COPYRIGHT = """\
/*
 * Copyright 2020 Intel Corporation
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
import math
import os

from mako.template import Template

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_entrypoints import get_entrypoints_from_xml

# We generate a static hash table for entry point lookup
# (vkGetProcAddress). We use a linear congruential generator for our hash
# function and a power-of-two size table. The prime numbers are determined
# experimentally.

TEMPLATE_H = Template(COPYRIGHT + """\
/* This file generated from ${filename}, don't edit directly. */

#ifndef VK_DISPATCH_TABLE_H
#define VK_DISPATCH_TABLE_H

#include "vulkan/vulkan.h"

#include "vk_extensions.h"

/* Windows api conflict */
#ifdef _WIN32
#include <windows.h>
#ifdef CreateSemaphore
#undef CreateSemaphore
#endif
#ifdef CreateEvent
#undef CreateEvent
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
VKAPI_ATTR void VKAPI_CALL vk_entrypoint_stub(void);
#endif

<%def name="dispatch_table(entrypoints)">
% for e in entrypoints:
  % if e.alias:
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
  % if e.aliases:
    union {
        PFN_vk${e.name} ${e.name};
      % for a in e.aliases:
        PFN_vk${a.name} ${a.name};
      % endfor
    };
  % else:
    PFN_vk${e.name} ${e.name};
  % endif
  % if e.guard is not None:
#else
    % if e.aliases:
    union {
        PFN_vkVoidFunction ${e.name};
      % for a in e.aliases:
        PFN_vkVoidFunction ${a.name};
      % endfor
    };
    % else:
    PFN_vkVoidFunction ${e.name};
    % endif
#endif
  % endif
% endfor
</%def>

<%def name="entrypoint_table(type, entrypoints)">
struct vk_${type}_entrypoint_table {
% for e in entrypoints:
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
    PFN_vk${e.name} ${e.name};
  % if e.guard is not None:
#else
    PFN_vkVoidFunction ${e.name};
# endif
  % endif
% endfor
};
</%def>

struct vk_instance_dispatch_table {
  ${dispatch_table(instance_entrypoints)}
};

struct vk_physical_device_dispatch_table {
  ${dispatch_table(physical_device_entrypoints)}
};

struct vk_device_dispatch_table {
  ${dispatch_table(device_entrypoints)}
};

struct vk_dispatch_table {
    union {
        struct {
            struct vk_instance_dispatch_table instance;
            struct vk_physical_device_dispatch_table physical_device;
            struct vk_device_dispatch_table device;
        };

        struct {
            ${dispatch_table(instance_entrypoints)}
            ${dispatch_table(physical_device_entrypoints)}
            ${dispatch_table(device_entrypoints)}
        };
    };
};

${entrypoint_table('instance', instance_entrypoints)}
${entrypoint_table('physical_device', physical_device_entrypoints)}
${entrypoint_table('device', device_entrypoints)}

<%def name="uncompacted_dispatch_table(entrypoints)">
% for e in entrypoints:
  % if e.alias:
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
    PFN_vk${e.name} ${e.name};
  % if e.aliases:
    % for a in e.aliases:
    PFN_vk${a.name} ${a.name};
    % endfor
  % endif
  % if e.guard is not None:
#else
    PFN_vkVoidFunction ${e.name};
    % if e.aliases:
      % for a in e.aliases:
        PFN_vkVoidFunction ${a.name};
      % endfor
    % endif
#endif
  % endif
% endfor
</%def>


struct vk_instance_uncompacted_dispatch_table {
  ${uncompacted_dispatch_table(instance_entrypoints)}
};

struct vk_physical_device_uncompacted_dispatch_table {
  ${uncompacted_dispatch_table(physical_device_entrypoints)}
};

struct vk_device_uncompacted_dispatch_table {
  ${uncompacted_dispatch_table(device_entrypoints)}
};

struct vk_uncompacted_dispatch_table {
    union {
        struct {
            struct vk_instance_uncompacted_dispatch_table instance;
            struct vk_physical_device_uncompacted_dispatch_table physical_device;
            struct vk_device_uncompacted_dispatch_table device;
        };

        struct {
            ${uncompacted_dispatch_table(instance_entrypoints)}
            ${uncompacted_dispatch_table(physical_device_entrypoints)}
            ${uncompacted_dispatch_table(device_entrypoints)}
        };
    };
};

void
vk_instance_dispatch_table_load(struct vk_instance_dispatch_table *table,
                                PFN_vkGetInstanceProcAddr gpa,
                                VkInstance instance);
void
vk_physical_device_dispatch_table_load(struct vk_physical_device_dispatch_table *table,
                                       PFN_vkGetInstanceProcAddr gpa,
                                       VkInstance instance);
void
vk_device_dispatch_table_load(struct vk_device_dispatch_table *table,
                              PFN_vkGetDeviceProcAddr gpa,
                              VkDevice device);

void
vk_instance_uncompacted_dispatch_table_load(struct vk_instance_uncompacted_dispatch_table *table,
                                PFN_vkGetInstanceProcAddr gpa,
                                VkInstance instance);
void
vk_physical_device_uncompacted_dispatch_table_load(struct vk_physical_device_uncompacted_dispatch_table *table,
                                       PFN_vkGetInstanceProcAddr gpa,
                                       VkInstance instance);
void
vk_device_uncompacted_dispatch_table_load(struct vk_device_uncompacted_dispatch_table *table,
                              PFN_vkGetDeviceProcAddr gpa,
                              VkDevice device);

void vk_instance_dispatch_table_from_entrypoints(
    struct vk_instance_dispatch_table *dispatch_table,
    const struct vk_instance_entrypoint_table *entrypoint_table,
    bool overwrite);

void vk_physical_device_dispatch_table_from_entrypoints(
    struct vk_physical_device_dispatch_table *dispatch_table,
    const struct vk_physical_device_entrypoint_table *entrypoint_table,
    bool overwrite);

void vk_device_dispatch_table_from_entrypoints(
    struct vk_device_dispatch_table *dispatch_table,
    const struct vk_device_entrypoint_table *entrypoint_table,
    bool overwrite);

PFN_vkVoidFunction
vk_instance_dispatch_table_get(const struct vk_instance_dispatch_table *table,
                               const char *name);

PFN_vkVoidFunction
vk_physical_device_dispatch_table_get(const struct vk_physical_device_dispatch_table *table,
                                      const char *name);

PFN_vkVoidFunction
vk_device_dispatch_table_get(const struct vk_device_dispatch_table *table,
                             const char *name);

PFN_vkVoidFunction
vk_instance_dispatch_table_get_if_supported(
    const struct vk_instance_dispatch_table *table,
    const char *name,
    uint32_t core_version,
    const struct vk_instance_extension_table *instance_exts);

PFN_vkVoidFunction
vk_physical_device_dispatch_table_get_if_supported(
    const struct vk_physical_device_dispatch_table *table,
    const char *name,
    uint32_t core_version,
    const struct vk_instance_extension_table *instance_exts);

PFN_vkVoidFunction
vk_device_dispatch_table_get_if_supported(
    const struct vk_device_dispatch_table *table,
    const char *name,
    uint32_t core_version,
    const struct vk_instance_extension_table *instance_exts,
    const struct vk_device_extension_table *device_exts);

#ifdef __cplusplus
}
#endif

#endif /* VK_DISPATCH_TABLE_H */
""")

TEMPLATE_C = Template(COPYRIGHT + """\
/* This file generated from ${filename}, don't edit directly. */

#include "vk_dispatch_table.h"

#include "../src/mesa/util/macros.h"
#include "string.h"

<%def name="load_dispatch_table(type, VkType, ProcAddr, entrypoints)">
void
vk_${type}_dispatch_table_load(struct vk_${type}_dispatch_table *table,
                               PFN_vk${ProcAddr} gpa,
                               ${VkType} obj)
{
% if type != 'physical_device':
    table->${ProcAddr} = gpa;
% endif
% for e in entrypoints:
  % if e.alias or e.name == '${ProcAddr}':
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
    table->${e.name} = (PFN_vk${e.name}) gpa(obj, "vk${e.name}");
  % for a in e.aliases:
    if (table->${e.name} == NULL) {
        table->${e.name} = (PFN_vk${e.name}) gpa(obj, "vk${a.name}");
    }
  % endfor
  % if e.guard is not None:
#endif
  % endif
% endfor
}
</%def>

${load_dispatch_table('instance', 'VkInstance', 'GetInstanceProcAddr',
                      instance_entrypoints)}

${load_dispatch_table('physical_device', 'VkInstance', 'GetInstanceProcAddr',
                      physical_device_entrypoints)}

${load_dispatch_table('device', 'VkDevice', 'GetDeviceProcAddr',
                      device_entrypoints)}

<%def name="load_uncompacted_dispatch_table(type, VkType, ProcAddr, entrypoints)">
void
vk_${type}_uncompacted_dispatch_table_load(struct vk_${type}_uncompacted_dispatch_table *table,
                               PFN_vk${ProcAddr} gpa,
                               ${VkType} obj)
{
% if type != 'physical_device':
    table->${ProcAddr} = gpa;
% endif
% for e in entrypoints:
  % if e.alias or e.name == '${ProcAddr}':
    <% continue %>
  % endif
  % if e.guard is not None:
#ifdef ${e.guard}
  % endif
    table->${e.name} = (PFN_vk${e.name}) gpa(obj, "vk${e.name}");
  % for a in e.aliases:
    table->${a.name} = (PFN_vk${a.name}) gpa(obj, "vk${a.name}");
    if (table->${e.name} && !table->${a.name})
       table->${a.name} = (PFN_vk${a.name}) table->${e.name};
    if (!table->${e.name})
       table->${e.name} = (PFN_vk${e.name}) table->${a.name};
  % endfor
  % if e.guard is not None:
#endif
  % endif
% endfor
}
</%def>

${load_uncompacted_dispatch_table('instance', 'VkInstance', 'GetInstanceProcAddr',
                      instance_entrypoints)}

${load_uncompacted_dispatch_table('physical_device', 'VkInstance', 'GetInstanceProcAddr',
                      physical_device_entrypoints)}

${load_uncompacted_dispatch_table('device', 'VkDevice', 'GetDeviceProcAddr',
                      device_entrypoints)}


struct string_map_entry {
   uint32_t name;
   uint32_t hash;
   uint32_t num;
};

/* We use a big string constant to avoid lots of reloctions from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

<%def name="strmap(strmap, prefix)">
static const char ${prefix}_strings[] =
% for s in strmap.sorted_strings:
    "${s.string}\\0"
% endfor
;

static const struct string_map_entry ${prefix}_string_map_entries[] = {
% for s in strmap.sorted_strings:
    { ${s.offset}, ${'{:0=#8x}'.format(s.hash)}, ${s.num} }, /* ${s.string} */
% endfor
};

/* Hash table stats:
 * size ${len(strmap.sorted_strings)} entries
 * collisions entries:
% for i in range(10):
 *     ${i}${'+' if i == 9 else ' '}     ${strmap.collisions[i]}
% endfor
 */

#define none 0xffff
static const uint16_t ${prefix}_string_map[${strmap.hash_size}] = {
% for e in strmap.mapping:
    ${ '{:0=#6x}'.format(e) if e >= 0 else 'none' },
% endfor
};

static int
${prefix}_string_map_lookup(const char *str)
{
    static const uint32_t prime_factor = ${strmap.prime_factor};
    static const uint32_t prime_step = ${strmap.prime_step};
    const struct string_map_entry *e;
    uint32_t hash, h;
    uint16_t i;
    const char *p;

    hash = 0;
    for (p = str; *p; p++)
        hash = hash * prime_factor + *p;

    h = hash;
    while (1) {
        i = ${prefix}_string_map[h & ${strmap.hash_mask}];
        if (i == none)
           return -1;
        e = &${prefix}_string_map_entries[i];
        if (e->hash == hash && strcmp(str, ${prefix}_strings + e->name) == 0)
            return e->num;
        h += prime_step;
    }

    return -1;
}
</%def>

${strmap(instance_strmap, 'instance')}
${strmap(physical_device_strmap, 'physical_device')}
${strmap(device_strmap, 'device')}

<% assert len(instance_entrypoints) < 2**8 %>
static const uint8_t instance_compaction_table[] = {
% for e in instance_entrypoints:
    ${e.disp_table_index},
% endfor
};

<% assert len(physical_device_entrypoints) < 2**8 %>
static const uint8_t physical_device_compaction_table[] = {
% for e in physical_device_entrypoints:
    ${e.disp_table_index},
% endfor
};

<% assert len(device_entrypoints) < 2**16 %>
static const uint16_t device_compaction_table[] = {
% for e in device_entrypoints:
    ${e.disp_table_index},
% endfor
};

static bool
vk_instance_entrypoint_is_enabled(int index, uint32_t core_version,
                                  const struct vk_instance_extension_table *instance)
{
   switch (index) {
% for e in instance_entrypoints:
   case ${e.entry_table_index}:
      /* ${e.name} */
   % if e.core_version:
      return ${e.core_version.c_vk_version()} <= core_version;
   % elif e.extensions:
     % for ext in e.extensions:
        % if ext.type == 'instance':
      if (instance->${ext.name[3:]}) return true;
        % else:
      /* All device extensions are considered enabled at the instance level */
      return true;
        % endif
     % endfor
      return false;
   % else:
      return true;
   % endif
% endfor
   default:
      return false;
   }
}

/** Return true if the core version or extension in which the given entrypoint
 * is defined is enabled.
 *
 * If device is NULL, all device extensions are considered enabled.
 */
static bool
vk_physical_device_entrypoint_is_enabled(int index, uint32_t core_version,
                                         const struct vk_instance_extension_table *instance)
{
   switch (index) {
% for e in physical_device_entrypoints:
   case ${e.entry_table_index}:
      /* ${e.name} */
   % if e.core_version:
      return ${e.core_version.c_vk_version()} <= core_version;
   % elif e.extensions:
     % for ext in e.extensions:
        % if ext.type == 'instance':
      if (instance->${ext.name[3:]}) return true;
        % else:
      /* All device extensions are considered enabled at the instance level */
      return true;
        % endif
     % endfor
      return false;
   % else:
      return true;
   % endif
% endfor
   default:
      return false;
   }
}

/** Return true if the core version or extension in which the given entrypoint
 * is defined is enabled.
 *
 * If device is NULL, all device extensions are considered enabled.
 */
static bool
vk_device_entrypoint_is_enabled(int index, uint32_t core_version,
                                const struct vk_instance_extension_table *instance,
                                const struct vk_device_extension_table *device)
{
   switch (index) {
% for e in device_entrypoints:
   case ${e.entry_table_index}:
      /* ${e.name} */
   % if e.core_version:
      return ${e.core_version.c_vk_version()} <= core_version;
   % elif e.extensions:
     % for ext in e.extensions:
        % if ext.type == 'instance':
      if (instance->${ext.name[3:]}) return true;
        % else:
      if (!device || device->${ext.name[3:]}) return true;
        % endif
     % endfor
      return false;
   % else:
      return true;
   % endif
% endfor
   default:
      return false;
   }
}

#ifdef _MSC_VER
VKAPI_ATTR void VKAPI_CALL vk_entrypoint_stub(void)
{
   UNREACHABLE("Entrypoint not implemented");
}

static const void *get_function_target(const void *func)
{
   const uint8_t *address = func;
#ifdef _M_X64
   /* Incremental linking may indirect through relative jump */
   if (*address == 0xE9)
   {
      /* Compute JMP target if the first byte is opcode 0xE9 */
      uint32_t offset;
      memcpy(&offset, address + 1, 4);
      address += offset + 5;
   }
#else
   /* Add other platforms here if necessary */
#endif
   return address;
}

static bool vk_function_is_stub(PFN_vkVoidFunction func)
{
   return (func == vk_entrypoint_stub) || (get_function_target(func) == get_function_target(vk_entrypoint_stub));
}
#endif

<%def name="dispatch_table_from_entrypoints(type)">
void vk_${type}_dispatch_table_from_entrypoints(
    struct vk_${type}_dispatch_table *dispatch_table,
    const struct vk_${type}_entrypoint_table *entrypoint_table,
    bool overwrite)
{
    PFN_vkVoidFunction *disp = (PFN_vkVoidFunction *)dispatch_table;
    PFN_vkVoidFunction *entry = (PFN_vkVoidFunction *)entrypoint_table;

    if (overwrite) {
        memset(dispatch_table, 0, sizeof(*dispatch_table));
        for (unsigned i = 0; i < ARRAY_SIZE(${type}_compaction_table); i++) {
#ifdef _MSC_VER
            assert(entry[i] != NULL);
            if (vk_function_is_stub(entry[i]))
#else
            if (entry[i] == NULL)
#endif
                continue;
            unsigned disp_index = ${type}_compaction_table[i];
            assert(disp[disp_index] == NULL);
            disp[disp_index] = entry[i];
        }
    } else {
        for (unsigned i = 0; i < ARRAY_SIZE(${type}_compaction_table); i++) {
            unsigned disp_index = ${type}_compaction_table[i];
#ifdef _MSC_VER
            assert(entry[i] != NULL);
            if (disp[disp_index] == NULL && !vk_function_is_stub(entry[i]))
#else
            if (disp[disp_index] == NULL)
#endif
                disp[disp_index] = entry[i];
        }
    }
}
</%def>

${dispatch_table_from_entrypoints('instance')}
${dispatch_table_from_entrypoints('physical_device')}
${dispatch_table_from_entrypoints('device')}

<%def name="lookup_funcs(type)">
static PFN_vkVoidFunction
vk_${type}_dispatch_table_get_for_entry_index(
    const struct vk_${type}_dispatch_table *table, int entry_index)
{
    assert(entry_index >= 0);
    assert((size_t)entry_index < ARRAY_SIZE(${type}_compaction_table));
    int disp_index = ${type}_compaction_table[entry_index];
    return ((PFN_vkVoidFunction *)table)[disp_index];
}

PFN_vkVoidFunction
vk_${type}_dispatch_table_get(
    const struct vk_${type}_dispatch_table *table, const char *name)
{
    int entry_index = ${type}_string_map_lookup(name);
    if (entry_index < 0)
        return NULL;

    return vk_${type}_dispatch_table_get_for_entry_index(table, entry_index);
}
</%def>

${lookup_funcs('instance')}
${lookup_funcs('physical_device')}
${lookup_funcs('device')}

PFN_vkVoidFunction
vk_instance_dispatch_table_get_if_supported(
    const struct vk_instance_dispatch_table *table,
    const char *name,
    uint32_t core_version,
    const struct vk_instance_extension_table *instance_exts)
{
    int entry_index = instance_string_map_lookup(name);
    if (entry_index < 0)
        return NULL;

    if (!vk_instance_entrypoint_is_enabled(entry_index, core_version,
                                           instance_exts))
        return NULL;

    return vk_instance_dispatch_table_get_for_entry_index(table, entry_index);
}

PFN_vkVoidFunction
vk_physical_device_dispatch_table_get_if_supported(
    const struct vk_physical_device_dispatch_table *table,
    const char *name,
    uint32_t core_version,
    const struct vk_instance_extension_table *instance_exts)
{
    int entry_index = physical_device_string_map_lookup(name);
    if (entry_index < 0)
        return NULL;

    if (!vk_physical_device_entrypoint_is_enabled(entry_index, core_version,
                                                  instance_exts))
        return NULL;

    return vk_physical_device_dispatch_table_get_for_entry_index(table, entry_index);
}

PFN_vkVoidFunction
vk_device_dispatch_table_get_if_supported(
    const struct vk_device_dispatch_table *table,
    const char *name,
    uint32_t core_version,
    const struct vk_instance_extension_table *instance_exts,
    const struct vk_device_extension_table *device_exts)
{
    int entry_index = device_string_map_lookup(name);
    if (entry_index < 0)
        return NULL;

    if (!vk_device_entrypoint_is_enabled(entry_index, core_version,
                                         instance_exts, device_exts))
        return NULL;

    return vk_device_dispatch_table_get_for_entry_index(table, entry_index);
}
""")

U32_MASK = 2**32 - 1

PRIME_FACTOR = 5024183
PRIME_STEP = 19

class StringIntMapEntry:
    def __init__(self, string, num):
        self.string = string
        self.num = num

        # Calculate the same hash value that we will calculate in C.
        h = 0
        for c in string:
            h = ((h * PRIME_FACTOR) + ord(c)) & U32_MASK
        self.hash = h

        self.offset = None

def round_to_pow2(x):
    return 2**int(math.ceil(math.log(x, 2)))

class StringIntMap:
    def __init__(self):
        self.baked = False
        self.strings = {}

    def add_string(self, string, num):
        assert not self.baked
        assert string not in self.strings
        assert 0 <= num < 2**31
        self.strings[string] = StringIntMapEntry(string, num)

    def bake(self):
        self.sorted_strings = \
            sorted(self.strings.values(), key=lambda x: x.string)
        offset = 0
        for entry in self.sorted_strings:
            entry.offset = offset
            offset += len(entry.string) + 1

        # Save off some values that we'll need in C
        self.hash_size = round_to_pow2(len(self.strings) * 1.25)
        self.hash_mask = self.hash_size - 1
        self.prime_factor = PRIME_FACTOR
        self.prime_step = PRIME_STEP

        self.mapping = [-1] * self.hash_size
        self.collisions = [0] * 10
        for idx, s in enumerate(self.sorted_strings):
            level = 0
            h = s.hash
            while self.mapping[h & self.hash_mask] >= 0:
                h = h + PRIME_STEP
                level = level + 1
            self.collisions[min(level, 9)] += 1
            self.mapping[h & self.hash_mask] = idx

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', help='Output C file.')
    parser.add_argument('--out-h', help='Output H file.')
    parser.add_argument('--beta', required=True, help='Enable beta extensions.')
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True,
                        action='append',
                        dest='xml_files')
    args = parser.parse_args()

    entrypoints = get_entrypoints_from_xml(args.xml_files, args.beta)

    device_entrypoints = []
    physical_device_entrypoints = []
    instance_entrypoints = []
    for e in entrypoints:
        if e.is_device_entrypoint():
            device_entrypoints.append(e)
        elif e.is_physical_device_entrypoint():
            physical_device_entrypoints.append(e)
        else:
            instance_entrypoints.append(e)

    for i, e in enumerate(e for e in device_entrypoints if not e.alias):
        e.disp_table_index = i

    device_strmap = StringIntMap()
    for i, e in enumerate(device_entrypoints):
        e.entry_table_index = i
        device_strmap.add_string("vk" + e.name, e.entry_table_index)
    device_strmap.bake()

    for i, e in enumerate(e for e in physical_device_entrypoints if not e.alias):
        e.disp_table_index = i

    physical_device_strmap = StringIntMap()
    for i, e in enumerate(physical_device_entrypoints):
        e.entry_table_index = i
        physical_device_strmap.add_string("vk" + e.name, e.entry_table_index)
    physical_device_strmap.bake()

    for i, e in enumerate(e for e in instance_entrypoints if not e.alias):
        e.disp_table_index = i

    instance_strmap = StringIntMap()
    for i, e in enumerate(instance_entrypoints):
        e.entry_table_index = i
        instance_strmap.add_string("vk" + e.name, e.entry_table_index)
    instance_strmap.bake()

    # For outputting entrypoints.h we generate a anv_EntryPoint() prototype
    # per entry point.
    try:
        if args.out_h:
            with open(args.out_h, 'w', encoding='utf-8') as f:
                f.write(TEMPLATE_H.render(instance_entrypoints=instance_entrypoints,
                                          physical_device_entrypoints=physical_device_entrypoints,
                                          device_entrypoints=device_entrypoints,
                                          filename=os.path.basename(__file__)))
        if args.out_c:
            with open(args.out_c, 'w', encoding='utf-8') as f:
                f.write(TEMPLATE_C.render(instance_entrypoints=instance_entrypoints,
                                          physical_device_entrypoints=physical_device_entrypoints,
                                          device_entrypoints=device_entrypoints,
                                          instance_strmap=instance_strmap,
                                          physical_device_strmap=physical_device_strmap,
                                          device_strmap=device_strmap,
                                          filename=os.path.basename(__file__)))
    except Exception:
        # In the event there's an error, this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        import sys
        from mako import exceptions
        print(exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
