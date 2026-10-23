#!/usr/bin/env python3
"""Resolve optional Mesa entrypoint table slots for Mach-O's strict linker.

ELF permits undefined weak references to resolve to zero. ld64 requires an
undefined-symbol policy even for weak references. Instead of weakening the
driver's entire link, inspect the actual compiled inputs and write NULL only
into table slots whose optional weak implementation is absent. Real functions
and strong dependencies remain subject to the normal strict final link.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

def resolve(build):
    build=build.resolve()
    target='src/panfrost/vulkan/libvulkan_panfrost.dylib'
    commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True)
    command=shlex.split(commands.splitlines()[-1])
    inputs=[]
    for token in command:
        path=build/token
        if path.suffix in ('.a','.o'):
            if not path.exists():raise RuntimeError(f'compile input is missing: {path}')
            inputs.append(path)
    if not inputs:raise RuntimeError('no compiled link inputs')
    symbols=subprocess.check_output(['llvm-nm-18','--defined-only','--extern-only',
                                    '--just-symbol-name',*(str(p) for p in inputs)],text=True)
    definitions={line[1:] for line in symbols.splitlines() if re.fullmatch(r'_[A-Za-z][A-Za-z0-9_]*',line)}
    raw=subprocess.check_output(['llvm-nm-18','-m',*(str(p) for p in inputs)],text=True)
    missing={symbol[1:] for symbol in re.findall(r'\(undefined\) weak external (_\w+)',raw)
             if symbol[1:] not in definitions}
    records=[]
    for source in sorted(build.rglob('*entrypoints.c')):
        text=source.read_text()
        if '#define VK_ENTRY_USE_WEAK 1' not in text:continue
        original=text
        replaced=[]
        def slot(match):
            prefix,name,suffix=match.groups()
            if name not in missing:return match.group(0)
            replaced.append(name)
            return prefix+'NULL'+suffix
        text=re.sub(r'(?m)^(\s*\.\w+\s*=\s*)(\w+)(,\s*)$',slot,text)
        if text!=original:
            source.write_text(text,newline='\n')
            records.append({'path':str(source.relative_to(build)),'slots':replaced,
                            'input_sha256':hashlib.sha256(original.encode()).hexdigest(),
                            'output_sha256':hashlib.sha256(text.encode()).hexdigest()})
    manifest=build/'macho-optional-entrypoints.json'
    if records or not manifest.exists():
        history=json.loads(manifest.read_text()).get('history',[]) if manifest.exists() else []
        history.append({'defined_symbols':len(definitions),'tables':records})
        manifest.write_text(json.dumps({'defined_symbols':len(definitions),'tables':records,'history':history},indent=2)+'\n')
    print(f'{sum(len(r["slots"]) for r in records)} absent optional slots resolved to NULL; {manifest}')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    resolve(p.parse_args().build)
