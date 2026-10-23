#!/usr/bin/env python3
"""Build and package the pinned arm64 iOS PanVK ICD using Clang/ld64.lld.

On Windows this launches the Linux build helper in WSL. Dependencies and all
outputs stay under --work, outside the source checkout. prepare_mesa.py must
first reconstruct the exact RC3 source. No device is accessed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True

def wsl_path(path):
    value=str(Path(path).resolve())
    drive,tail=os.path.splitdrive(value)
    if len(drive)!=2 or drive[1]!=':':raise ValueError('WSL input must be on a Windows drive')
    return '/mnt/'+drive[0].lower()+tail.replace('\\','/')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--sysroot',type=Path,required=True)
    p.add_argument('--darwin-headers',type=Path)
    p.add_argument('--libcxx-headers',default='/usr/lib/llvm-18/include/c++/v1')
    p.add_argument('--distribution',default='Ubuntu')
    p.add_argument('--jobs',type=int,default=6)
    p.add_argument('--configure-only',action='store_true')
    a=p.parse_args()
    if not a.darwin_headers:
        import ziglang
        a.darwin_headers=Path(ziglang.__file__).parent/'lib/libc/include/any-darwin-any'
    if sys.platform=='win32':
        work=wsl_path(a.work)
        command=['wsl.exe','-d',a.distribution,'--exec','env','PYTHONDONTWRITEBYTECODE=1']
        modules=a.work/'panvk-hosttools-linux'
        if modules.exists():command+=['PYTHONPATH='+wsl_path(modules)]
        command+=['python3',wsl_path(Path(__file__).with_name('build_mesa_wsl.py')),
            '--work',work,'--sysroot',wsl_path(a.sysroot),
            '--darwin-headers',wsl_path(a.darwin_headers),'--libcxx-headers',a.libcxx_headers,
            '--jobs',str(a.jobs)]
        if a.configure_only:command+=['--configure-only']
        subprocess.run(command,check=True)
    else:
        from build_mesa_wsl import build
        build(a.work,a.sysroot,a.darwin_headers,Path(a.libcxx_headers),a.jobs,a.configure_only)

if __name__=='__main__':main()
