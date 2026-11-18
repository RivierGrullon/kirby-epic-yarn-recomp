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
import tomllib

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from build_info import collect
from package_portable import project_snapshot
from installer.verification import check_pe


def package(root,output):
    if os.name!='nt':raise ValueError('Build Windows setup on Windows with native Python and PyInstaller')
    root,output=Path(root).resolve(),Path(output).absolute()
    if output.exists():raise ValueError('Setup output already exists')
    expected=collect(root,compiler='pending',build_type='Release')
    if expected['source_modified']:raise ValueError('Commit sources before packaging')
    launcher=root/'build/native/kirby-launcher.exe';check_pe(launcher)
    information=tomllib.loads(subprocess.check_output([launcher,'--build-info'],text=True,encoding='utf-8'))
    if any(information['build'][key]!=expected[key] for key in ('source_revision','source_modified','source_sha256')):
        raise ValueError('Rebuild the launcher from the committed setup sources')
    output.parent.mkdir(parents=True,exist_ok=True)
    # Qt uses Windows ICU. An unrelated icuuc.dll on PATH can break the frozen GUI.
    system=Path(os.environ['SystemRoot'])/'System32'
    freeze_env=dict(os.environ,PATH=os.pathsep.join(map(str,(Path(sys.executable).parent,
                                                             Path(sys.base_prefix),system,system.parent))))
    with tempfile.TemporaryDirectory(prefix='.setup-',dir=output.parent) as temporary:
        temporary=Path(temporary)
        for name,script,extra in (('kirby-setup-worker','tools/install.py',['--exclude-module','PyQt6']),
                                  ('Kirby-Setup','tools/setup_gui.py',['--windowed'])):
            subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--name',name,
                '--distpath',str(temporary/'dist'),'--workpath',str(temporary/'work'),
                '--specpath',str(temporary/'spec'),'--contents-directory','python',*extra,script],cwd=root,env=freeze_env,check=True)
        staged=temporary/'Kirby-Setup';shutil.copytree(temporary/'dist/Kirby-Setup',staged)
        shutil.copytree(temporary/'dist/kirby-setup-worker',staged/'tools/runtime')
        project_snapshot(root,staged/'project')
        native=staged/'project/build/native';native.mkdir(parents=True)
        shutil.copy2(launcher,native/launcher.name)
        for library in launcher.parent.glob('*.dll'):shutil.copy2(library,native/library.name)
        # Qt must load on a machine before the worker can install the VC++ runtime.
        # Bundle the native runtime DLLs available on the packaging machine.
        for name in ('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll',
                     'vcruntime140.dll','vcruntime140_1.dll'):
            if (system/name).is_file():shutil.copy2(system/name,staged/'python'/name)
        (staged/'host-libs').mkdir()
        (staged/'revision').write_text(expected['source_revision']+'\n')
        for name in ('LICENSE','THIRD_PARTY.md'):shutil.copy2(root/name,staged/name)
        notices=staged/'licenses/qt';notices.mkdir(parents=True)
        for name in ('PyQt6','PyQt6-Qt6','PyQt6-sip'):
            path=Path(importlib.metadata.distribution(name)._path)
            shutil.copytree(path,notices/path.name)
        env=dict(os.environ,QT_QPA_PLATFORM='offscreen',PATH=str(system))
        subprocess.run([staged/'Kirby-Setup.exe','--self-test'],env=env,cwd=staged,check=True,timeout=30)
        os.rename(staged,output)
    archive=Path(shutil.make_archive(str(output),'zip',root_dir=output.parent,base_dir=output.name))
    print('Windows setup ready:',archive)
    return archive


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:package(Path(__file__).resolve().parents[2],args.output)
    except (OSError,ValueError,subprocess.SubprocessError) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()
