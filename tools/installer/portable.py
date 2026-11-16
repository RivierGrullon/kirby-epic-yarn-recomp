# SPDX-License-Identifier: GPL-3.0-only
"""Private Linux build tools; no host interpreter or container engine required.

PRoot translates paths for build/extraction subprocesses only. It is not a
security sandbox. Native game verification runs on the host, outside PRoot.
"""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re


class PortableTools:
    REQUIRED = ('proot', 'rootfs/usr/bin/python3.12', 'rootfs/usr/bin/git',
                'rootfs/usr/bin/clang', 'rootfs/usr/bin/cmake', 'rootfs/usr/bin/ninja',
                'rootfs/usr/bin/7z', 'rootfs/usr/bin/dolphin-tool', 'rootfs/usr/bin/dotnet')

    @classmethod
    def find(cls, root):
        base = (Path(root).resolve()/'tools/runtime').resolve()
        marker = base/'toolchain.json'
        if not marker.exists() and not marker.is_symlink():
            return None
        if marker.is_symlink() or not marker.is_file() or marker.stat().st_size > 131072:
            raise ValueError('Invalid portable toolchain manifest')
        record = json.loads(marker.read_text(encoding='utf-8'))
        if (not isinstance(record, dict) or record.get('version') != 1 or record.get('platform') != 'linux-x86_64'
                or not isinstance(record.get('image_id'), str)
                or not re.fullmatch('[a-f0-9]{64}', record['image_id'])
                or not isinstance(record.get('files'), dict)
                or not set(cls.REQUIRED).issubset(record['files'])):
            raise ValueError('Incomplete portable Linux toolchain')
        for name, expected in record['files'].items():
            path = PurePosixPath(name)
            if (not name or path.is_absolute() or '..' in path.parts or '\\' in name
                    or not (base/name).resolve().is_relative_to(base)):
                raise ValueError('Invalid portable tool path')
            target = base/name
            if (not isinstance(expected, str) or not re.fullmatch('[a-f0-9]{64}', expected)
                    or not target.is_file()):
                raise ValueError('Missing portable tool: '+name)
            with target.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
                    raise ValueError('Portable tool checksum mismatch: '+name)
        if (base/'rootfs').is_symlink() or not (base/'rootfs').is_dir():
            raise ValueError('Invalid portable root filesystem')
        return cls(Path(root).resolve(), base, record['image_id'])

    def __init__(self, root, base, image_id):
        self.root, self.base, self.image_id = root, base, image_id

    def command(self, arguments, *, binds=()):
        if not arguments:
            raise ValueError('A portable tool command is required')
        temporary = self.root/'local/setup/portable-tmp'
        temporary.mkdir(parents=True, exist_ok=True, mode=0o700)
        if temporary.is_symlink():
            raise ValueError('Portable temporary directory must not be a symlink')
        paths = [(self.root, '/workspace'), (self.root, str(self.root)),
                 (temporary, '/tmp')]
        for path in binds:
            path = Path(path).resolve(strict=True)
            if not path.is_relative_to(self.root):
                paths.append((path, str(path)))
        command = [str(self.base/'proot'), '-r', str(self.base/'rootfs')]
        for path in ('/dev', '/proc', '/sys', '/etc/resolv.conf', '/etc/hosts'):
            if Path(path).exists():
                command += ['-b', path+':'+path+'!']
        for source, destination in dict.fromkeys(paths):
            if ':' in str(source) or ':' in destination:
                raise ValueError('Portable setup paths cannot contain a colon')
            command += ['-b', str(source)+':'+destination]
        return [*command, '-w', '/workspace', *map(os.fspath, arguments)]

    def environment(self, inherited=None):
        env = dict(os.environ if inherited is None else inherited)
        for name in list(env):
            if name.startswith(('PYTHON', 'SDL_', 'VK_', '__GL', '__NV', 'LD_')) or name in (
                    'DISPLAY', 'WAYLAND_DISPLAY', 'DOTNET_ROOT', 'GIT_CONFIG_COUNT'):
                env.pop(name, None)
        env.update(PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
                   LD_LIBRARY_PATH=str(self.base/'lib'), HOME='/tmp/kirby-build-home',
                   TMPDIR='/tmp', DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_NOLOGO='1',
                   GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='safe.directory', GIT_CONFIG_VALUE_0='/workspace')
        return env
