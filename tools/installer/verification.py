# SPDX-License-Identifier: GPL-3.0-only
"""Verify the copied native executable without touching normal saves or audio."""
import os
from pathlib import Path
import struct
import tempfile
import tomllib

from kirby import EXPECTED_SHA1
from .jobs import run_process


def check_elf(path):
    with Path(path).open('rb') as stream:header=stream.read(64)
    if (len(header)<64 or header[:6]!=b'\x7fELF\x02\x01' or
            struct.unpack_from('<H',header,16)[0] not in (2,3) or struct.unpack_from('<H',header,18)[0]!=62):
        raise ValueError('Build output is not a Linux x86-64 ELF executable')


def verify_native(job,root,executable,data_root,identity):
    root=Path(root);executable=Path(executable).resolve()
    check_elf(executable)
    environment=dict(os.environ,SDL_AUDIO_DRIVER='dummy')
    libraries=root/'local/host-libs'
    if libraries.is_dir():
        environment['LD_LIBRARY_PATH']=str(libraries)+(os.pathsep+environment['LD_LIBRARY_PATH'] if environment.get('LD_LIBRARY_PATH') else '')
    document=run_process(job,[executable,'--build-info'],capture_limit=16384,timeout=10,env=environment)
    info=tomllib.loads(document.decode('utf-8'))
    try:
        build=info['build']
        if (info['schema_version']!=1 or info['program']!='runtime' or build['game_id']!='RK5E01'
                or build['game_revision']!=0 or build['dol_sha1']!=EXPECTED_SHA1
                or any(build[key]!=value for key,value in identity.items())):
            raise ValueError('Compiled build provenance does not match the selected source checkout')
    except (KeyError,TypeError) as error:
        raise ValueError('Native executable has malformed build information') from error
    with tempfile.TemporaryDirectory(prefix='native-verify-',dir=job.workspace/'staging') as directory:
        options=['--volume','0','--game-data',str(data_root)]
        for name,mode in (('bootstrap','--verify-bootstrap'),('init','--probe-init'),('disc','--probe-disc'),('es','--probe-es-metadata')):
            job.emit(dict(stage='verify',state='started',check=name))
            run_process(job,[executable,mode,*options],timeout=20,cwd=directory,env=environment)
        job.emit(dict(stage='verify',state='started',check='video'))
        markers=(b'WPAD initialized',b'verified AXWii task loaded',b'first GX draw submitted')
        seen=set();tail=bytearray()
        def observe(data):
            tail.extend(data)
            if len(tail)>8192:del tail[:-8192]
            for marker in markers:
                if marker in tail:seen.add(marker)
        frames=Path(directory)/'frames.csv'
        def ready():
            if len(seen)!=len(markers) or not frames.is_file():return False
            with frames.open('rb') as stream:return len(stream.read(8192).splitlines())>=2
        run_process(job,[executable,'--video-hidden',*options,'--frame-log',frames],timeout=35,
                    cwd=directory,env=environment,on_output=observe,stop_when=ready)
        if not ready():
            raise ValueError('Native startup did not submit a verified first frame')
    return build
