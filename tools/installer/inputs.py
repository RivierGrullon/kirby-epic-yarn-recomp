# SPDX-License-Identifier: GPL-3.0-only
"""Separate translation inputs from runtime provenance and bound compile work."""
import hashlib
import os
from pathlib import Path

from kirby import EXPECTED_SHA1
from .installations import canonical
from .platforms import WINDOWS, executable_name


def tree_digest(root,paths,check_cancel=lambda:None):
    value=hashlib.sha256()
    for path in sorted(set(paths)):
        check_cancel()
        name=path.relative_to(root).as_posix().encode()
        value.update(len(name).to_bytes(8,'big')+name)
        if path.is_symlink():
            raise ValueError(f'Build input must not be a symlink: {path}')
        if not path.exists():
            value.update(b'missing\0');continue
        if not path.is_file():
            raise ValueError(f'Build input must be a regular file: {path}')
        digest=hashlib.sha256()
        with path.open('rb') as stream:
            while chunk:=stream.read(1024*1024):check_cancel();digest.update(chunk)
        value.update(digest.digest())
    return value.hexdigest()


def generated_digest(path,check_cancel=lambda:None):
    return tree_digest(path,[p for p in path.rglob('*') if p.is_file() or p.is_symlink()],check_cancel)


def build_keys(root,identity,image_id,check_cancel=lambda:None,*,target=None):
    target=target or ("windows" if WINDOWS else "linux")
    executable_name(target)  # Reject an unknown compilation target.
    root=Path(root)
    paths=[root/name for name in ('projects/RK5E01/recomp.yml','tools/kirby.py','tools/build_layout.py','dependencies.json',
                                  'vendor/key/config/RK5E01/config.yml','vendor/key/config/RK5E01/symbols.txt',
                                  'vendor/key/config/RK5E01/splits.txt')]
    for name in ('runtime/native-src','runtime/include','tools/audits','patches/wiicompiled','patches/aurora'):
        paths.extend(p for p in (root/name).rglob('*') if p.is_file() or p.is_symlink())
    # Bytecode caches are not authored translator inputs.
    paths=[p for p in paths if '__pycache__' not in p.parts and p.suffix!='.pyc']
    translation=hashlib.sha256(canonical(dict(version=1,dol_sha1=EXPECTED_SHA1,
                    inputs=tree_digest(root,paths,check_cancel)))).hexdigest()
    cache=hashlib.sha256(canonical(dict(translation=translation,image_id=image_id,target=target))).hexdigest()
    native=hashlib.sha256(canonical(dict(translation=translation,image_id=image_id,project=identity,target=target))).hexdigest()
    return dict(translation=translation,cache=cache,native=native)


def compile_jobs(cpus,available):
    gib=1024**3
    if available<3*gib:
        raise ValueError('Native compilation requires at least 3 GiB of available memory')
    return max(1,min(4,max(1,cpus),int((available-1.5*gib)//(1.5*gib))))


def host_compile_jobs():
    if WINDOWS:
        return compile_jobs(os.cpu_count() or 1, windows_available_memory())
    values={line.split(':',1)[0]:int(line.split()[1])*1024
            for line in Path('/proc/meminfo').read_text().splitlines() if line.startswith('MemAvailable:')}
    available=values['MemAvailable']
    cpus=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else os.cpu_count() or 1
    try:
        line=next(line for line in Path('/proc/self/cgroup').read_text().splitlines() if line.startswith('0::'))
        group=Path('/sys/fs/cgroup')/line[3:].lstrip('/')
        limit=(group/'memory.max').read_text().strip()
        if limit!='max':available=min(available,max(0,int(limit)-int((group/'memory.current').read_text())))
        quota,period=(group/'cpu.max').read_text().split()
        if quota!='max':cpus=min(cpus,max(1,int(quota)//int(period)))
    except (OSError,StopIteration,ValueError):
        pass
    return compile_jobs(cpus,available)


def windows_available_memory():
    import ctypes
    from ctypes import wintypes
    class MemoryStatus(ctypes.Structure):
        _fields_=[('length',wintypes.DWORD),('load',wintypes.DWORD),
                  ('total_physical',ctypes.c_ulonglong),('available_physical',ctypes.c_ulonglong),
                  ('total_page',ctypes.c_ulonglong),('available_page',ctypes.c_ulonglong),
                  ('total_virtual',ctypes.c_ulonglong),('available_virtual',ctypes.c_ulonglong),
                  ('extended_virtual',ctypes.c_ulonglong)]
    status=MemoryStatus();status.length=ctypes.sizeof(status)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GlobalMemoryStatusEx.argtypes=[ctypes.POINTER(MemoryStatus)]
    if not kernel.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise ctypes.WinError(ctypes.get_last_error())
    return status.available_physical
