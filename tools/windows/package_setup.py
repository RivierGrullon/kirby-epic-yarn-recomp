#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Freeze a native Windows PyQt setup folder, without a translated game executable."""
import argparse
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from package_portable import project_snapshot
from installer.qt_frontend import copy_payload


def package(root,output,qt_frontend):
    if os.name!='nt':raise ValueError('Build Windows setup on Windows with native Python and PyInstaller')
    root,output=Path(root).resolve(),Path(output).absolute()
    if output.exists():raise ValueError('Setup output already exists')
    output.parent.mkdir(parents=True,exist_ok=True)
    # Qt uses Windows ICU. An unrelated icuuc.dll on PATH can break the frozen GUI.
    system=Path(os.environ['SystemRoot'])/'System32'
    freeze_env=dict(os.environ,PATH=os.pathsep.join(map(str,(Path(sys.executable).parent,
                                                             Path(sys.base_prefix),system,system.parent))))
    with tempfile.TemporaryDirectory(prefix='.setup-',dir=output.parent) as temporary:
        temporary=Path(temporary)
        for name,script,extra in (('kirby-setup-worker','tools/install.py',['--exclude-module','PyQt6']),
                                  ('Kirby-Bridge','tools/launcher_bridge.py',['--exclude-module','PyQt6']),
                                  ('Kirby-Setup','tools/setup_gui.py',['--windowed'])):
            subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--name',name,
                '--distpath',str(temporary/'dist'),'--workpath',str(temporary/'work'),
                '--specpath',str(temporary/'spec'),'--contents-directory',
                'bridge-python' if name=='Kirby-Bridge' else 'python',*extra,script],cwd=root,env=freeze_env,check=True)
        staged=temporary/'Kirby-Setup';shutil.copytree(temporary/'dist/Kirby-Setup',staged)
        # Bridge uses bridge-python, so its freeze cannot replace Qt's Python files.
        shutil.copytree(temporary/'dist/Kirby-Bridge',staged,dirs_exist_ok=True)
        shutil.copytree(temporary/'dist/kirby-setup-worker',staged/'tools/runtime')
        copy_payload(qt_frontend, staged/'tools/qt-launcher')
        revision=project_snapshot(root,staged/'project')
        from launcher_profiles import write_profile
        write_profile(root,staged,revision)
        # Qt must load on a machine before the worker can install the VC++ runtime.
        # Bundle the native runtime DLLs available on the packaging machine.
        for name in ('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll',
                     'vcruntime140.dll','vcruntime140_1.dll'):
            if (system/name).is_file():shutil.copy2(system/name,staged/'python'/name)
        (staged/'host-libs').mkdir()
        (staged/'revision').write_text(revision+'\n')
        for name in ('LICENSE','THIRD_PARTY.md'):shutil.copy2(root/name,staged/name)
        notices=staged/'licenses/qt';notices.mkdir(parents=True)
        for name in ('PyQt6','PyQt6-Qt6','PyQt6-sip'):
            path=Path(importlib.metadata.distribution(name)._path)
            shutil.copytree(path,notices/path.name)
        env=dict(os.environ,QT_QPA_PLATFORM='offscreen',PATH=str(system))
        subprocess.run([staged/'Kirby-Setup.exe','--self-test'],env=env,cwd=temporary,check=True,timeout=30)
        subprocess.run([staged/'Kirby-Bridge.exe','--bridge-help'],env=env,cwd=temporary,check=True,timeout=30)
        subprocess.run([staged/'tools/qt-launcher/kirby-ui.exe','--self-test'],env=env,cwd=temporary,check=True,timeout=60)
        # Windows can briefly retain a handle to the tested executable.
        deadline = time.monotonic() + 10
        while True:
            try:
                os.rename(staged,output)
                break
            except PermissionError:
                if output.exists() or time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)
    archive=Path(shutil.make_archive(str(output),'zip',root_dir=output.parent,base_dir=output.name))
    print('Windows setup ready:',archive)
    return archive


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--qt-frontend',type=Path,default=Path('build/native/qt-launcher'),
                        help='Deployed C++ frontend directory, including the Qt runtime')
    args=parser.parse_args()
    try:package(Path(__file__).resolve().parents[2],args.output,args.qt_frontend)
    except (OSError,ValueError,subprocess.SubprocessError) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()
