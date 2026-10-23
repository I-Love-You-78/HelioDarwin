#!/usr/bin/env python3
"""Generate Vulkan query serialization fields from the pinned public headers."""
import argparse
import re
from pathlib import Path

def generate(header,out):
    source=header.read_text()
    lines=['/* Generated from the pinned Khronos Vulkan core declarations. */',
           '/* SPDX-License-Identifier: MIT */','#ifndef MMVK_PROFILE_FIELDS_H',
           '#define MMVK_PROFILE_FIELDS_H','']
    structs=('VkPhysicalDeviceFeatures','VkPhysicalDeviceVulkan11Features',
             'VkPhysicalDeviceVulkan12Features','VkPhysicalDeviceLimits',
             'VkPhysicalDeviceVulkan11Properties','VkPhysicalDeviceVulkan12Properties')
    for name in structs:
        block=re.search(r'typedef struct '+name+r'\s*\{(.*?)\}\s*'+name+r';',source,re.S).group(1)
        fields=[]
        for typ,field,count in re.findall(r'\b(\w+)\s+(\w+)\s*(?:\[([^]]+)\])?;',block):
            if field in ('sType','pNext') or typ in ('char','VkConformanceVersion'): continue
            if count: fields.append(f'    MMVK_JSON_ARRAY(j, "{field}", p->{field}, {count});')
            elif typ=='VkBool32':fields.append(f'    MMVK_JSON_BOOL(j, "{field}", p->{field});')
            elif typ in ('VkSampleCountFlags','VkShaderStageFlags','VkSubgroupFeatureFlags','VkResolveModeFlags'):
                fields.append(f'    json_flags(j, "{field}", p->{field}, {typ}_names);')
            elif typ in ('VkDriverId','VkShaderFloatControlsIndependence','VkPointClippingBehavior'):
                fields.append(f'    json_enum(j, "{field}", p->{field}, {typ}_names);')
            elif typ=='float':fields.append(f'    json_float(j, "{field}", p->{field});')
            elif typ in ('int32_t',):fields.append(f'    json_signed(j, "{field}", p->{field});')
            elif typ in ('uint32_t','uint64_t','VkDeviceSize','size_t','uint8_t'):
                fields.append(f'    json_unsigned(j, "{field}", p->{field});')
            else: raise ValueError(f'unsupported property type: {typ} {field}')
        lines += [f'static void serialize_{name}(struct json_writer *j, const {name} *p)',
                  '{',*fields,'}','']
    # Enum names are only emitted for real numeric values; aliases do not add
    # duplicate flag spellings or manufacture unqueried bits.
    enums={'VkSampleCountFlagBits':'VkSampleCountFlags','VkShaderStageFlagBits':'VkShaderStageFlags',
           'VkSubgroupFeatureFlagBits':'VkSubgroupFeatureFlags','VkResolveModeFlagBits':'VkResolveModeFlags',
           'VkDriverId':'VkDriverId','VkShaderFloatControlsIndependence':'VkShaderFloatControlsIndependence',
           'VkPointClippingBehavior':'VkPointClippingBehavior','VkFormatFeatureFlagBits':'VkFormatFeatureFlags',
           'VkFormat':'VkFormat','VkPhysicalDeviceType':'VkPhysicalDeviceType'}
    tables=[]
    for enum,table in enums.items():
        block=re.search(r'typedef enum '+enum+r'\s*\{(.*?)\}\s*'+enum+r';',source,re.S).group(1)
        entries=[]
        for name,value in re.findall(r'\b(VK_\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,',block):
            number=int(value,0)
            if 'MAX_ENUM' in name or (enum=='VkFormat' and not 0<number<=184):continue
            entries.append(f'    {{ {name}, "{name}" }},')
        tables += [f'static const struct json_name {table}_names[] = {{',*entries,'    {0,NULL}','};','']
    # Tables must precede the serializers which consume them.
    lines=lines[:5]+tables+lines[5:]+['#endif','']
    out.write_text('\n'.join(lines),newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--header',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();generate(a.header,a.out)
