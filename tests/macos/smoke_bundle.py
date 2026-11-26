# SPDX-License-Identifier: GPL-3.0-only
"""Test a relocated app with the real launcher and an asset-free game stand-in."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from macos.package_macos import package


def main():
    arch, qt_prefix=sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix='kirby-bundle-smoke-') as temporary:
        fixture=Path(temporary).resolve();build=fixture/f'build/macos-{arch}'
        (build/'qt-launcher').mkdir(parents=True)
        host=ROOT/'build/macos-host-tests/kirby-launcher'
        shutil.copy2(host,build/'kirby-launcher')
        # It answers --build-info, contains no translated game code, and is
        # never launched as a game. This fixture validates packaging only.
        shutil.copy2(host,build/'WiiCompiled')
        shutil.copy2(ROOT/'build/macos-qt-tests/kirby-ui',build/'qt-launcher/kirby-ui')
        (build/'dsp_coef.bin').write_bytes(b'synthetic packaging fixture')
        for name in ('LICENSE','THIRD_PARTY.md','dependencies.json'):
            shutil.copy2(ROOT/name,fixture/name)
        original=package(fixture,arch,Path(qt_prefix))
        relocated=fixture/'Relocated app with spaces.app'
        original.rename(relocated)
        binary=relocated/'Contents/MacOS'
        environment={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','QT_','QML','LD_LIBRARY_PATH'))}
        environment.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin',SDL_AUDIO_DRIVER='dummy',QT_QPA_PLATFORM='offscreen')
        subprocess.run([binary/'kirby-launcher','--self-test'],cwd=fixture,env=environment,check=True,timeout=90)
        result=subprocess.run([binary/'kirby-launcher','--ui-service'],input='{"cmd":"hello"}\n',
                              cwd=fixture,env=environment,capture_output=True,text=True,check=True,timeout=30)
        replies=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        states=[reply['state'] for reply in replies if 'state' in reply]
        if not states:
            raise RuntimeError('The relocated launcher did not answer hello: '+result.stdout+result.stderr)
        state=states[-1]
        if Path(state['executable']) != binary/'WiiCompiled':
            raise RuntimeError('The app did not resolve its adjacent runtime')
        settings=Path(state['settings'])
        if settings.is_relative_to(relocated) or 'Application Support' not in settings.parts:
            raise RuntimeError('Bundle settings would not survive an application update')
        if list(relocated.rglob('settings.toml')):
            raise RuntimeError('A writable settings file was created inside the app')
        print('Relocated app: Qt, native service, executable lookup and external settings directory passed.')


if __name__=='__main__':
    main()
