#!/usr/bin/env python3
"""Reconstruct the pinned PanVK JM-v9 dependency outside the source checkout.

The dependency is intentionally not vendored into HelioDarwin. Patches retain
their original licences; a manifest records every input and formatting repair.
No existing dirty Mesa checkout is reset or cleaned by this helper.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

PANVK_REPO = 'https://github.com/zenithblue-oss/panvk-kbase-android.git'
PANVK_COMMIT = 'fd6ed91eff1c8d3a83307a2348b959bb93797011'
MESA_REPO = 'https://gitlab.freedesktop.org/mesa/mesa.git'
MESA_COMMIT = '5a07217f034b3e50d8c7c7794f97a2df1742613b'
FAMILIES = ('common','android','kbase-common','app-loader','wsi','csf','csf-v11','jm-v9')

def run(*args, cwd=None, check=True, **kwargs):
    return subprocess.run(args,cwd=cwd,check=check,**kwargs)

def clone_exact(url, commit, target):
    if target.exists():
        head=run('git','rev-parse','HEAD',cwd=target,capture_output=True,text=True).stdout.strip()
        if head!=commit:
            raise RuntimeError(f'{target}: expected {commit}, found {head}')
        return
    target.mkdir(parents=True)
    run('git','init',str(target))
    run('git','remote','add','origin',url,cwd=target)
    run('git','-c','http.version=HTTP/1.1','fetch','--depth','1','origin',commit,cwd=target)
    run('git','-c','core.autocrlf=false','checkout','--detach','FETCH_HEAD',cwd=target)

def normalize_patch(text):
    # Some RC3 patches have unprefixed blank context lines or a leading space
    # before a diff header. Repair formatting only, never added/removed code.
    lines=[]
    hunk=False
    for line in text.splitlines():
        if line.lstrip().startswith('diff --git '):
            line=line.lstrip()
            hunk=False
        if line.startswith('@@ '): hunk=True
        if hunk and not line: line=' '
        lines.append(line)
    return '\n'.join(lines)+'\n'

def prepare(work, mesa_repo):
    work=work.resolve()
    repository=Path(__file__).resolve().parents[2]
    if work==repository or repository in work.parents:
        raise ValueError('dependency work directory must be outside the Git checkout')
    source=work/'panvk-rc3'
    mesa=work/'mesa-v9'
    clone_exact(PANVK_REPO,PANVK_COMMIT,source)
    clone_exact(mesa_repo,MESA_COMMIT,mesa)
    manifest=work/'mesa-jm-v9-provenance.json'
    if manifest.exists():
        raise RuntimeError(f'{manifest} already exists; use the prepared dependency or a new work directory')
    dirty=run('git','status','--porcelain',cwd=mesa,capture_output=True,text=True).stdout
    if dirty:
        raise RuntimeError('Mesa checkout is modified; refusing to reapply patches')
    records=[]
    for family in FAMILIES:
        for patch in sorted((source/'patches'/family).glob('*.patch')):
            raw=patch.read_bytes()
            fixed=work/'normalized-input.patch'
            fixed.write_text(normalize_patch(raw.decode()),encoding='utf-8',newline='\n')
            selected=None
            for path,flags in ((patch,()),(fixed,()),(fixed,('--recount',))):
                result=run('git','apply','--check',*flags,str(path),cwd=mesa,
                           check=False,capture_output=True,text=True)
                if result.returncode==0:
                    selected=(path,flags)
                    break
            if selected is None:
                raise RuntimeError(f'{patch}: {result.stderr}')
            path,flags=selected
            run('git','apply',*flags,str(path),cwd=mesa)
            records.append({'path':str(patch.relative_to(source)).replace('\\','/'),
                            'sha256':hashlib.sha256(raw).hexdigest(),
                            'format_repaired':path==fixed,'recount':bool(flags)})
        overlay=source/'patches'/family/'files'
        if overlay.exists(): shutil.copytree(overlay,mesa,dirs_exist_ok=True)
    manifest.write_text(json.dumps({'panvk_url':PANVK_REPO,'panvk_commit':PANVK_COMMIT,
        'mesa_url':MESA_REPO,'fetch_mirror':mesa_repo,'mesa_commit':MESA_COMMIT,
        'families':FAMILIES,'patches':records},indent=2)+'\n')
    print(manifest)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--mesa-repo',default=MESA_REPO,
                        help='optional Mesa mirror; the exact commit is still verified')
    args=parser.parse_args()
    prepare(args.work,args.mesa_repo)
