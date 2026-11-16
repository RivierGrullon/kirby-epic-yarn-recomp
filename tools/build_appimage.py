#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wrap a prepared AppDir in a pinned AppImage runtime, without game files."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
# Official AppImage/type2-runtime fedfad51acc2860b2916a1eef32ba4a500ff31ef.
# The continuous URL can change; never accept bytes that do not match this pin.
RUNTIME_URL = 'https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64'
RUNTIME_SHA256 = '617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46'


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def runtime_file():
    cache=ROOT/'local/packaging';cache.mkdir(parents=True,exist_ok=True)
    runtime=cache/('runtime-'+RUNTIME_SHA256[:12]+'-x86_64')
    if not runtime.exists():
        with tempfile.NamedTemporaryFile(dir=cache,delete=False) as stream:
            temporary=Path(stream.name)
            try:
                request=urllib.request.Request(RUNTIME_URL,headers={'User-Agent':'Kirby-native-packager'})
                with urllib.request.urlopen(request,timeout=60) as response:
                    shutil.copyfileobj(response,stream)
                stream.flush()
                if sha256(temporary) != RUNTIME_SHA256:
                    raise ValueError('Downloaded AppImage runtime checksum mismatch')
                temporary.replace(runtime)
            finally:
                temporary.unlink(missing_ok=True)
    if sha256(runtime) != RUNTIME_SHA256:
        raise ValueError('Cached AppImage runtime checksum mismatch')
    runtime.chmod(0o700)
    return runtime


def build(appdir, output):
    appdir,output=Path(appdir).resolve(),Path(output).absolute()
    for name in ('AppRun','revision','project/tools/kirby.py','tools/runtime/toolchain.json'):
        if not (appdir/name).is_file():
            raise ValueError('Incomplete AppDir: '+name)
    if output.exists():
        raise ValueError('AppImage output already exists')
    if not shutil.which('mksquashfs'):
        raise ValueError('The developer packager needs squashfs-tools (mksquashfs)')
    runtime=runtime_file()
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.appimage-',dir=output.parent) as temporary:
        filesystem=Path(temporary)/'filesystem.squashfs'
        subprocess.run(['mksquashfs',appdir,filesystem,'-noappend','-all-root','-no-xattrs',
                        '-comp','zstd','-processors','2','-no-progress'],check=True)
        candidate=Path(temporary)/'image'
        with candidate.open('xb') as stream:
            for path in (runtime,filesystem):
                with path.open('rb') as source:
                    shutil.copyfileobj(source,stream)
        candidate.chmod(0o755);candidate.replace(output)
    checksum=sha256(output)
    output.with_suffix(output.suffix+'.sha256').write_text(checksum+'  '+output.name+'\n')
    print('AppImage ready:',output)
    print('SHA-256:',checksum)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appdir',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    build(args.appdir,args.output)


if __name__=='__main__':
    main()
