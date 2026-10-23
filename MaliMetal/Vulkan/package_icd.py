#!/usr/bin/env python3
"""Relink, inspect and package a successfully compiled SweetMali iOS ICD.

Requires the WSL/Linux build tree and llvm-nm-18. The strict final link uses
the exact Ninja inputs; no unresolved-symbol relaxation is added. Output and
third-party licences remain outside the HelioDarwin Git checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
from prepare_mesa import MESA_COMMIT,PANVK_COMMIT,PANVK_REPO,MESA_REPO,FAMILIES

ICD_INSTALL_NAME='/System/Library/Extensions/MaliMetal.bundle/libpanvk_sweetmali.dylib'

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def input_maps(root):
    compiled_names={'MaliKmodDarwin.c','MaliKmodDarwin.h','mesa/pan_kmod_xnu.c'}
    files={str(p.relative_to(root)).replace('\\','/'):digest(p) for p in sorted(root.rglob('*'))
           if p.is_file() and p.suffix in ('.c','.h','.py')}
    compiled={name:sha for name,sha in files.items() if name in compiled_names}
    tools={name:sha for name,sha in files.items() if name.endswith('.py')}
    bundle={name:sha for name,sha in files.items() if name not in compiled and name not in tools}
    return compiled,tools,bundle

def inspect(path):
    data=path.read_bytes()
    magic,cpu,_,kind,count,_,_,_=struct.unpack_from('<8I',data)
    if (magic,cpu,kind)!=(0xfeedfacf,0x100000c,6):raise ValueError('not an arm64 Mach-O dylib')
    offset=32;platform=None;uuid=None;dylibs=[];install_name=None;symtab=None
    for _ in range(count):
        cmd,size=struct.unpack_from('<II',data,offset)
        if size<8 or offset+size>len(data):raise ValueError('invalid Mach-O command')
        if cmd==0x32:platform=struct.unpack_from('<I',data,offset+8)[0]
        if cmd==0x1b:uuid=data[offset+8:offset+24].hex()
        if cmd==2:symtab=struct.unpack_from('<4I',data,offset+8)
        if cmd in (0xc,0xd,0x80000018,0x8000001f):
            start=offset+struct.unpack_from('<I',data,offset+8)[0]
            name=data[start:data.index(0,start,offset+size)].decode()
            if cmd==0xd:install_name=name
            else:dylibs.append(name)
        offset+=size
    if platform!=2 or not uuid:raise ValueError(f'expected iOS platform2 and LC_UUID, got {platform}')
    if install_name!=ICD_INSTALL_NAME:raise ValueError(f'incorrect ICD install name: {install_name}')
    if not symtab:raise ValueError('missing symbol table')
    symoff,nsyms,stroff,strsize=symtab;exports=[];imports=[];weak=[]
    for index in range(nsyms):
        string,typ,section,desc,value=struct.unpack_from('<IBBHQ',data,symoff+index*16)
        if not string or not typ&1 or typ&0xe0:continue
        name=data[stroff+string:data.index(0,stroff+string,stroff+strsize)].decode()
        if typ&0xe:exports.append(name)
        else:
            imports.append(name)
            if desc&0x40:weak.append(name)
    required={'_panvk_xnu_bind_connection','_vk_icdGetInstanceProcAddr',
              '_vk_icdGetPhysicalDeviceProcAddr','_vk_icdNegotiateLoaderICDInterfaceVersion'}
    if not required.issubset(exports):raise ValueError(f'missing required exports: {required-set(exports)}')
    own=re.compile(r'^_(?:panvk|vk_|wsi_|mmk_|drm|pan_kmod|bi_|va_|_mesa|poly_)')
    unresolved=[name for name in imports if own.match(name)]
    if unresolved:raise ValueError(f'unresolved project symbols: {unresolved[:20]}')
    return {'platform':'iOS','platform_id':platform,'cpu':'arm64','uuid':uuid,
            'install_name':install_name,'required_exports':sorted(required),
            'dynamic_libraries':dylibs,'import_count':len(imports),
            'weak_imports':weak,'unresolved_project_symbols':unresolved}

def package(work):
    work=work.resolve();build=work/'mesa-ios-clang-build';mesa=work/'mesa-v9'
    root=Path(__file__).resolve().parent
    output=work/'dist-ios';output.mkdir(exist_ok=True)
    target='src/panfrost/vulkan/libvulkan_panfrost.dylib'
    if not (build/target).exists():raise ValueError('build the complete ICD first')
    commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True)
    arguments=shlex.split(commands.splitlines()[-1])
    binary=output/'libpanvk_sweetmali.dylib'
    arguments[arguments.index('-o')+1]=str(binary)
    found=False
    for n,arg in enumerate(arguments):
        if arg.startswith('-Wl,-install_name,'):
            arguments[n]='-Wl,-install_name,'+ICD_INSTALL_NAME;found=True
    if not found:raise ValueError('Ninja link command has no install name')
    if any('dynamic_lookup' in arg for arg in arguments):raise ValueError('strict link required')
    arguments.append('-Wl,-adhoc_codesign')
    subprocess.run(arguments,cwd=build,check=True)
    details=inspect(binary)
    source=work/'panvk-rc3'
    patch_inputs=[]
    for family in FAMILIES:
        for path in sorted((source/'patches'/family).glob('*.patch')):
            patch_inputs.append({'path':str(path.relative_to(source)),'sha256':digest(path)})
    compiled,tools,bundle=input_maps(root)
    data={'manifest_format':2,'binary':binary.name,'binary_size':binary.stat().st_size,'binary_sha256':digest(binary),
          'mesa':{'url':MESA_REPO,'commit':MESA_COMMIT},
          'panvk_rc3':{'url':PANVK_REPO,'commit':PANVK_COMMIT,'patches':patch_inputs},
          'xnu_port_inputs':compiled,'build_tool_inputs':tools,
          'bundle_backend_inputs_informational':bundle,
          'kernel_abi_sha256':digest(root.parent/'MaliMetalKernelABI.h'),
          'port_manifest':json.loads((work/'mesa-xnu-port.json').read_text()),
          'optional_entrypoints':json.loads((build/'macho-optional-entrypoints.json').read_text()),
          'clang_version':subprocess.check_output(['clang-18','--version'],text=True).splitlines()[0],
          'linker_version':subprocess.check_output(['ld64.lld-18','--version'],text=True).strip(),
          'link_command_sha256':hashlib.sha256(json.dumps(arguments).encode()).hexdigest(),
          'macho':details,'hardware_execution':'not performed',
          'transport':{'abi':1,'frontend':'Valhall v9 Job Manager','gpu_id':'0x90930010',
                       'submission':'synchronous hardware completion','cpu_mapping':'Normal non-cacheable',
                       'dma_buf_import':False,'iosurface_import':False,'grow_on_fault':False}}
    licences=output/'LICENSES';licences.mkdir(exist_ok=True)
    shutil.copytree(mesa/'licenses',licences/'Mesa',dirs_exist_ok=True)
    shutil.copytree(source/'LICENSES',licences/'PanVK-RC3',dirs_exist_ok=True)
    shutil.copy2(mesa/'docs/license.rst',licences/'Mesa-license.rst')
    shutil.copy2(source/'NOTICE.md',licences/'PanVK-RC3-NOTICE.md')
    (output/'panvk-sweetmali.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({key:data[key] for key in ('binary','binary_size','binary_sha256','hardware_execution')},indent=2))
    print(output/'panvk-sweetmali.json')

def refresh_manifest(work):
    """Refresh informational bundle hashes without relinking the ICD.

    The binary receipt and its compiled sources must remain unchanged. Build
    tool hashes retain the versions which actually produced that receipt.
    """
    root=Path(__file__).resolve().parent;work=work.resolve()
    manifest=work/'dist-ios/panvk-sweetmali.json'
    data=json.loads(manifest.read_text())
    binary=manifest.parent/data['binary']
    if digest(binary)!=data['binary_sha256']:raise ValueError('ICD binary differs from its build receipt')
    inspect(binary)
    compiled,tools,bundle=input_maps(root)
    old_inputs={name.replace('\\','/'):sha for name,sha in data['xnu_port_inputs'].items()}
    for name,sha in compiled.items():
        if old_inputs.get(name)!=sha:raise ValueError(f'ICD compiled input changed: {name}; rebuild required')
    if digest(root.parent/'MaliMetalKernelABI.h')!=data['kernel_abi_sha256']:
        raise ValueError('ICD kernel ABI changed; rebuild required')
    data['manifest_format']=2;data['xnu_port_inputs']=compiled
    if 'build_tool_inputs' not in data:
        data['build_tool_inputs']={name:sha for name,sha in old_inputs.items() if name.endswith('.py')}
    data['bundle_backend_inputs_informational']=bundle
    data['manifest_generator_sha256']=digest(Path(__file__).resolve())
    manifest.write_text(json.dumps(data,indent=2)+'\n')
    print(f'ICD binary unchanged: {data["binary_sha256"]}; {manifest}')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True)
    p.add_argument('--refresh-manifest',action='store_true',help='refresh bundle-only informational hashes; never relink')
    a=p.parse_args()
    if a.refresh_manifest:refresh_manifest(a.work)
    else:package(a.work)
