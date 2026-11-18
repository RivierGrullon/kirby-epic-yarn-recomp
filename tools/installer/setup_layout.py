# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a persistent installation independently of the setup application's mount."""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from .platforms import WINDOWS, is_link


@dataclass(frozen=True)
class Bundle:
    project: Path
    runtime: Path
    libraries: Path
    revision: str
    command: tuple = ()
    packaged: bool = False


def _atomic(path, body, executable=False):
    path = Path(path)
    if is_link(path):
        raise ValueError('Setup output must not be a link')
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.setup-',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as stream:
            stream.write(body);stream.flush();os.fsync(stream.fileno())
        if executable:os.chmod(name,0o755)
        os.replace(name,path)
    finally:Path(name).unlink(missing_ok=True)


def prepare_destination(bundle, destination):
    source = Path(bundle.project).resolve()
    destination = Path(destination).resolve()
    if not re.fullmatch('[a-f0-9]{40}',bundle.revision):
        raise ValueError('Invalid setup source revision')
    # A short directory keeps Windows paths manageable; the marker checks the full pin.
    root = destination/'.kirby'/bundle.revision[:12]
    if root.resolve().is_relative_to(source):
        raise ValueError('Choose an installation folder outside the setup sources')
    for path in [*reversed(destination.parents),destination,destination/'.kirby',root]:
        if is_link(path):raise ValueError('Installation folder must not contain links')
    root.parent.mkdir(parents=True,exist_ok=True)
    marker=root/'local/setup/source.json'
    if root.exists():
        if is_link(marker) or not marker.is_file() or json.loads(marker.read_text()) != {'revision':bundle.revision}:
            raise ValueError('This installation folder contains unrecognized files; choose another folder')
        if bundle.command:
            _atomic(root/'local/setup-command.json',json.dumps(dict(version=1,arguments=[*bundle.command,'--destination',str(destination)])).encode('utf-8'))
        return root
    with tempfile.TemporaryDirectory(prefix='.prepare-',dir=root.parent) as temporary:
        staged=Path(temporary)/'project'
        if not bundle.packaged:
            # A development worktree's .git pointer is never an installed checkout.
            subprocess.run(['git','clone','--quiet','--no-local','--depth','1',source.as_uri(),str(staged)],check=True)
            subprocess.run(['git','-C',str(staged),'remote','remove','origin'],check=True)
        else:
            ignored={'.tools','local','vendor','disc','generated','build','build-logs','__pycache__','.superpowers'}
            def ignore(directory,names):
                return [name for name in names if name=='__pycache__' or
                        (Path(directory)==source and name in ignored) or
                        (Path(directory)==source/'tools' and name=='runtime')]
            shutil.copytree(source,staged,symlinks=True,ignore=ignore)
        if not (staged/'.git').is_dir() or not (staged/'tools/kirby.py').is_file():
            raise ValueError('Setup requires a complete authored source snapshot')
        runtime=staged/'tools/runtime';runtime.mkdir(parents=True,exist_ok=True)
        for name in ('toolchain-download.json','ca-certificates.crt'):
            if (bundle.runtime/name).is_file():shutil.copy2(bundle.runtime/name,runtime/name)
        libraries=staged/'local/host-libs'
        if bundle.libraries.is_dir():shutil.copytree(bundle.libraries,libraries,symlinks=False)
        marker=staged/'local/setup/source.json';marker.parent.mkdir(parents=True,exist_ok=True)
        marker.write_text(json.dumps({'revision':bundle.revision})+'\n')
        if bundle.command:
            _atomic(staged/'local/setup-command.json',json.dumps(dict(version=1,arguments=[*bundle.command,'--destination',str(destination)])).encode('utf-8'))
        os.rename(staged,root)
    return root


def update_game_data(root, data_root):
    """Change only the known paths setting, preserving every other preference."""
    import tomllib
    settings=Path(root)/'local/user/settings.toml'
    if not settings.exists():
        return  # The launcher defaults to the verified installation's data root.
    if is_link(settings):raise ValueError('Settings must not be a link')
    original=settings.read_text(encoding='utf-8')
    before=tomllib.loads(original)
    value='dvd_root = '+json.dumps(str(Path(data_root).absolute()),ensure_ascii=False)+'\n'
    lines=original.splitlines(keepends=True)
    start=next((i for i,line in enumerate(lines) if re.fullmatch(r'\s*\[paths\]\s*(?:#.*)?',line.strip())),None)
    if start is None:
        if 'paths' in before:raise ValueError('Custom paths settings cannot be updated automatically')
        candidate=original.rstrip()+'\n\n[paths]\n'+value
    else:
        end=next((i for i in range(start+1,len(lines)) if lines[i].lstrip().startswith('[')),len(lines))
        key=next((i for i in range(start+1,end) if re.match(r'\s*dvd_root\s*=',lines[i])),end)
        if key==end:lines.insert(key,value)
        else:lines[key]=value
        candidate=''.join(lines)
    expected=dict(before);expected['paths']=dict(before.get('paths',{}),dvd_root=str(Path(data_root).absolute()))
    if tomllib.loads(candidate)!=expected:raise ValueError('Settings update would change other preferences')
    _atomic(settings,candidate.encode('utf-8'))


def finish_destination(root, destination, data_root=None):
    root,destination=Path(root).resolve(),Path(destination).resolve()
    launcher=root/'build/native'/('kirby-launcher.exe' if WINDOWS else 'kirby-launcher')
    if not launcher.is_file():raise ValueError('The installed launcher is missing')
    if data_root is not None:update_game_data(root,data_root)
    relative=launcher.relative_to(destination)
    if WINDOWS:
        shortcut=destination/'Play Kirby.cmd'
        body=('@echo off\r\nstart "" "%~dp0'+str(relative)+'"\r\n').encode('utf-8')
    else:
        import shlex
        shortcut=destination/'Play Kirby.sh'
        body=('#!/bin/sh\ncd -- "${0%/*}" || exit 1\nexec '+shlex.quote('./'+relative.as_posix())+' "$@"\n').encode()
    _atomic(shortcut,body,executable=True)
    return shortcut
