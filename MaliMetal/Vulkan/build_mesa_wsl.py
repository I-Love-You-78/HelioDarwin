#!/usr/bin/env python3
"""Build the pinned SweetMali PanVK ICD in Linux/WSL for arm64 iOS.

Uses real clang/ld64.lld and the extracted iOS sysroot. Host generators are
compiled from the same patched Mesa source, never taken from another release.
Dependencies: Clang/LLVM18+development headers, LLVMSPIRVLib18, SPIRV-Tools,
Meson>=1.4, Ninja, Mako, YAML, packaging, Flex and Bison. No device is accessed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True
from prepare_mesa import MESA_COMMIT
from apply_xnu import apply
from resolve_macho_entrypoints import resolve
from package_icd import package

def run(args,**kwargs): subprocess.run([str(a) for a in args],check=True,**kwargs)

def build(work,sysroot,darwin_headers,libcxx_headers,jobs,configure_only,qualify_g57_fp16=False):
    work=work.resolve();mesa=work/'mesa-v9'
    repository=Path(__file__).resolve().parents[2]
    if work==repository or repository in work.parents:
        raise ValueError('build work must remain outside the source checkout')
    if subprocess.check_output(['git','rev-parse','HEAD'],cwd=mesa,text=True).strip()!=MESA_COMMIT:
        raise ValueError('unexpected Mesa base')
    host=work/'mesa-host-build'
    meson=[sys.executable,'-m','mesonbuild.mesonmain']
    env={**os.environ,'CC':'clang-18','CXX':'clang++-18','PYTHONDONTWRITEBYTECODE':'1'}
    common=['-Dbuildtype=release','-Dgallium-drivers=','-Dvulkan-drivers=panfrost',
        '-Dplatforms=','-Dglx=disabled','-Degl=disabled','-Dgbm=disabled',
        '-Dopengl=false','-Dgles1=disabled','-Dgles2=disabled','-Dzstd=disabled',
        '-Dpanfrost-rust=false','-Dbuild-tests=false']
    if not (host/'build.ninja').exists():
        run([*meson,'setup',host,mesa,*common,'-Dmesa-clc=enabled',
             '-Dinstall-mesa-clc=true','-Dprecomp-compiler=enabled',
             '-Dinstall-precomp-compiler=true','-Dllvm=enabled'],env=env)
    run(['ninja','-C',host,'-j',jobs,'src/compiler/clc/mesa_clc',
         'src/compiler/spirv/vtn_bindgen2','src/panfrost/clc/panfrost_compile'])
    apply(work)
    tools=work/'ios-tools';tools.mkdir(exist_ok=True)
    # Declarations and licences only. Linux libdrm implementations are never
    # linked into the iOS driver. Unused DRM paths are pruned by the XNU port.
    import shutil
    for source in ('/usr/include/xf86drm.h','/usr/include/xf86drmMode.h',
                   '/usr/include/libdrm/drm.h','/usr/include/libdrm/drm_mode.h'):
        shutil.copy2(source,tools/Path(source).name)
    def quoted(x): return repr(str(x))
    args=['--target=arm64-apple-ios16.7','-mcpu=cortex-a55','-isysroot',str(sysroot),
          '-isystem',str(darwin_headers),'-I',str(tools),'-DMESA_XNU=1']
    if qualify_g57_fp16:args+=['-DPANVK_XNU_QUALIFY_G57_FP16=1']
    cpp_args=['-nostdinc++','-isystem',str(libcxx_headers),*args]
    cross=tools/'clang-ios.ini'
    lines=['# SweetMali clang cross configuration 2','[binaries]',
        'c = '+repr(['/usr/bin/clang-18',*args]),
        'objc = '+repr(['/usr/bin/clang-18',*args]),
        'cpp = '+repr(['/usr/bin/clang++-18',*cpp_args]),
        'ar = \'/usr/bin/llvm-ar-18\'',"strip = '/usr/bin/llvm-strip-18'",
        'pkg-config = \'/usr/bin/false\'',
        'mesa_clc = '+quoted(host/'src/compiler/clc/mesa_clc'),
        'vtn_bindgen2 = '+quoted(host/'src/compiler/spirv/vtn_bindgen2'),
        'panfrost_compile = '+quoted(host/'src/panfrost/clc/panfrost_compile'),
        '[host_machine]',"system = 'darwin'","cpu_family = 'aarch64'",
        "cpu = 'cortex_a55'","endian = 'little'",'[properties]',
        'needs_exe_wrapper = true','[built-in options]',
        "c_link_args = ['-fuse-ld=/usr/bin/ld64.lld-18', '-Wl,-platform_version,ios,16.7,16.7']",
        "cpp_link_args = ['-fuse-ld=/usr/bin/ld64.lld-18', '-Wl,-platform_version,ios,16.7,16.7']",
        "objc_link_args = ['-fuse-ld=/usr/bin/ld64.lld-18', '-Wl,-platform_version,ios,16.7,16.7']",'']
    cross_text='\n'.join(lines)
    changed=not cross.exists() or cross.read_text()!=cross_text
    cross.write_text(cross_text)
    target=work/'mesa-ios-clang-build'
    if target.resolve().parent!=work:raise ValueError('build target resolves outside --work')
    env['PATH']=os.pathsep.join(str(host/p) for p in
        ('src/compiler/clc','src/compiler/spirv','src/panfrost/clc'))+os.pathsep+env['PATH']
    if not (target/'build.ninja').exists() or changed:
        run([*meson,'setup',target,mesa,'--cross-file',cross,*common,
             *(['--wipe'] if (target/'build.ninja').exists() else []),
             '-Dpanfrost-kmds=kbase','-Dmesa-clc=system','-Dprecomp-compiler=system',
             '-Dllvm=disabled','-Dzlib=disabled','-Dexpat=disabled','-Dxmlconfig=disabled',
             '-Dshader-cache=disabled','-Dlibunwind=disabled'],env=env)
    if not configure_only:
        command=['ninja','-C',str(target),'-j',str(jobs),'src/panfrost/vulkan/libvulkan_panfrost.dylib']
        result=subprocess.run(command,env=env,capture_output=True,text=True)
        log=target/'sweetmali-build-output.log'
        log.write_text(result.stdout+result.stderr)
        if result.returncode:
            # resolve() refuses partial compiles and only changes absent weak
            # table slots. Strong missing implementations remain fatal.
            if 'undefined symbol:' not in result.stderr+result.stdout:
                print((result.stdout+result.stderr)[-8000:])
                raise subprocess.CalledProcessError(result.returncode,command)
            resolve(target)
            result=subprocess.run(command,env=env,capture_output=True,text=True)
            with log.open('a') as stream:stream.write(result.stdout+result.stderr)
            if result.returncode:
                print((result.stdout+result.stderr)[-8000:])
                raise subprocess.CalledProcessError(result.returncode,command)
        print(f'ICD compile/link passed; {log}')
        package(work)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--sysroot',type=Path,required=True)
    p.add_argument('--darwin-headers',type=Path,required=True)
    p.add_argument('--libcxx-headers',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=6)
    p.add_argument('--configure-only',action='store_true')
    p.add_argument('--qualify-g57-fp16',action='store_true',
                   help='experimental exact-G57 FP16 availability; numeric correctness remains unqualified')
    a=p.parse_args()
    build(a.work,a.sysroot,a.darwin_headers,a.libcxx_headers,a.jobs,a.configure_only,a.qualify_g57_fp16)
