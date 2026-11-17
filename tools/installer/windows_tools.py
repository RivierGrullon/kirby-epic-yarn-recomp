# SPDX-License-Identifier: GPL-3.0-only
"""Pinned private Windows tools for the standalone setup worker.

PowerShell owns downloading/checking archives; this adapter carries the resulting
paths and environment into each independently cancellable worker subprocess.
"""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
from urllib.parse import urlsplit

from .jobs import run_process
from .platforms import WINDOWS, is_link


def reset_frozen_dll_directory():
    """Do not let PyInstaller's DLL directory leak into private native tools."""
    if WINDOWS and getattr(sys, 'frozen', False):
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.SetDllDirectoryW.argtypes = [wintypes.LPCWSTR]
        kernel.SetDllDirectoryW.restype = wintypes.BOOL
        if not kernel.SetDllDirectoryW(None):
            raise ctypes.WinError(ctypes.get_last_error())


class WindowsTools:
    REQUIRED = ('python', 'git', 'cmake', 'ninja', 'dotnet', 'mingw', 'sdk',
                'dolphin', 'sevenzip', 'vcredist')

    @staticmethod
    def _manifest(root):
        path = Path(root) / 'tools/windows/windows-dependencies.json'
        if is_link(path) or not path.is_file() or path.stat().st_size > 65536:
            raise ValueError('Invalid Windows toolchain manifest')
        record = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(record, dict) or not set(WindowsTools.REQUIRED).issubset(record):
            raise ValueError('Incomplete Windows toolchain manifest')
        for name, spec in record.items():
            if not isinstance(name, str) or not re.fullmatch(r'[a-z][a-z0-9_-]*', name) or not isinstance(spec, dict):
                raise ValueError('Invalid Windows tool package')
            algorithm = spec.get('algorithm')
            length = {'SHA256': 64, 'SHA512': 128}.get(algorithm) if isinstance(algorithm, str) else None
            if (length is None or not isinstance(spec.get('hash'), str)
                    or not re.fullmatch('[a-f0-9]{' + str(length) + '}', spec['hash'])
                    or spec.get('format') not in ('zip', '7z', 'exe')):
                raise ValueError('Windows tool package requires a complete checksum pin')
            entry = spec.get('entry')
            if (not isinstance(entry, str) or not entry or PurePosixPath(entry).is_absolute()
                    or '..' in PurePosixPath(entry).parts or '\\' in entry or ':' in entry or '\0' in entry):
                raise ValueError('Unsafe Windows tool entry path')
            url = urlsplit(spec.get('url', '') if isinstance(spec.get('url'), str) else '')
            if url.scheme != 'https' or not url.hostname or url.username or url.password or url.fragment:
                raise ValueError('Windows tool download requires an HTTPS URL without credentials')
        identity = hashlib.sha256(json.dumps(record, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        return record, identity

    @classmethod
    def cache_path(cls, root):
        _, identity = cls._manifest(root)
        local = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData/Local').absolute()
        return local / 'KirbyNative/toolchains/windows' / identity[:16]

    @staticmethod
    def _check_owner(base, identity):
        marker = base / '.kirby-toolchain'
        if is_link(marker):
            raise ValueError('Windows toolchain identity marker must not be a link')
        if not marker.exists():
            return False
        if (not marker.is_file() or marker.stat().st_size > 128
                or marker.read_text(encoding='ascii').strip() != identity):
            raise ValueError('Windows toolchain cache belongs to another full identity')
        return True

    @classmethod
    def find(cls, root):
        if not WINDOWS:
            return None
        root = Path(root).resolve()
        if not (root / 'tools/windows/windows-dependencies.json').exists():
            return None
        record, identity = cls._manifest(root)
        shared = cls.cache_path(root)
        for base in (shared, root / '.tools/windows'):
            if not base.exists():
                continue
            for path in (base, *base.parents):
                if is_link(path):
                    raise ValueError('Windows toolchain cache must not be a link')
            if base == shared and not cls._check_owner(base, identity):
                continue
            entries = {}
            for name in cls.REQUIRED:
                spec = record[name]
                package = base / (name + '-' + spec['hash'][:16])
                marker, entry = package / '.complete', package / spec['entry']
                if not marker.exists():
                    break
                for path in (marker, entry, *entry.parents):
                    if path == base:
                        break
                    if is_link(path):
                        raise ValueError('Windows toolchain entries must not be links')
                if (not marker.is_file() or marker.stat().st_size > 256
                        or marker.read_text(encoding='utf-8-sig').strip() != spec['hash']):
                    raise ValueError('Windows toolchain checksum pin mismatch: ' + name)
                if not entry.is_file():
                    raise ValueError('Missing pinned Windows tool: ' + name)
                entries[name] = entry
            else:
                return cls(root, base, identity, entries)
        return None

    @classmethod
    def prepare(cls, root, job):
        job.check_cancel()
        if not WINDOWS:
            raise ValueError('Native Windows tools require Windows')
        root = Path(root).resolve()
        # Validate all pins before the downloader receives the manifest.
        cache = cls.cache_path(root)
        for path in (cache, *cache.parents):
            if is_link(path):
                raise ValueError('Windows toolchain cache must not be a link')
        _, identity = cls._manifest(root)
        cache.mkdir(parents=True, exist_ok=True)
        if not cls._check_owner(cache, identity):
            try:
                with (cache / '.kirby-toolchain').open('x', encoding='ascii') as stream:
                    stream.write(identity + '\n')
            except FileExistsError:
                if not cls._check_owner(cache, identity):
                    raise ValueError('Windows toolchain identity marker is missing')
        reset_frozen_dll_directory()
        powershell = shutil.which('powershell.exe') or shutil.which('powershell')
        if not powershell:
            raise ValueError('Windows PowerShell 5.1 is required to prepare the build tools')
        job.emit(dict(stage='prepare', state='started', detail='Preparing private Windows build tools'))
        run_process(job, [powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                         root / 'tools/windows/build-windows.ps1', '-ToolsOnly', '-CacheRoot', cache],
                    cwd=root)
        job.check_cancel()
        tools = cls.find(root)
        if tools is None:
            raise ValueError('Windows tool preparation did not produce a complete pinned toolchain')
        job.emit(dict(stage='prepare', state='complete', detail='Windows build tools are ready'))
        return tools

    def __init__(self, root, base, identity, entries):
        self.root, self.base, self.image_id = root, base, identity
        self.entries = entries
        self.mingw_root = entries['mingw'].parent.parent
        self.cppwinrt_include = entries['sdk'].parent.parent

    def environment(self, inherited=None):
        env = dict(os.environ if inherited is None else inherited)
        path = next((value for key, value in env.items() if key.upper() == 'PATH'), '')
        for key in list(env):
            if key.upper().startswith('PYTHON') or key.upper() in ('PATH', 'DOTNET_ROOT', 'KIRBY_TRANSLATOR_DLL'):
                env.pop(key)
        bins = dict.fromkeys(str(self.entries[name].parent) for name in
                             ('python', 'git', 'cmake', 'ninja', 'dotnet', 'mingw', 'dolphin', 'sevenzip'))
        env.update(PATH=os.pathsep.join((*bins, path)), DOTNET_ROOT=str(self.entries['dotnet'].parent),
                   DOTNET_MULTILEVEL_LOOKUP='0', DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_NOLOGO='1',
                   PYTHONUTF8='1', KIRBY_PROJECT_ROOT=str(self.root))
        return env

    def command(self, arguments):
        if not arguments:
            raise ValueError('A native Windows tool command is required')
        arguments = [os.fspath(value) for value in arguments]
        aliases = {'python3': 'python', 'python': 'python', 'python.exe': 'python',
                   '7z': 'sevenzip', '7zz': 'sevenzip', '7zr': 'sevenzip',
                   'DolphinTool.exe': 'dolphin', 'dolphin-tool': 'dolphin',
                   'git': 'git', 'dotnet': 'dotnet', 'cmake': 'cmake', 'ninja': 'ninja'}
        name = aliases.get(arguments[0])
        if name is None:
            return arguments
        arguments[0] = str(self.entries[name])
        if name != 'python' or len(arguments) < 2:
            return arguments
        tools = str(self.root / 'tools')
        if arguments[1] == '-c' and len(arguments) > 2:
            # Embedded Python ignores PYTHONPATH and excludes the script directory.
            arguments[2] = 'import sys;sys.path.insert(0,' + repr(tools) + ');' + arguments[2]
        elif arguments[1].endswith('.py'):
            script = Path(arguments[1])
            script = script if script.is_absolute() else self.root / script
            rest = arguments[2:]
            if script == self.root / 'tools/build_native.py':
                if '--llvm-mingw-root' not in rest:
                    rest += ['--llvm-mingw-root', str(self.mingw_root)]
                if '--cppwinrt-include' not in rest:
                    rest += ['--cppwinrt-include', str(self.cppwinrt_include)]
            runner = ('import runpy,sys;sys.path.insert(0,sys.argv[1]);'
                      'sys.argv=sys.argv[2:];runpy.run_path(sys.argv[0],run_name="__main__")')
            arguments = [arguments[0], '-c', runner, tools, str(script), *rest]
        return arguments
