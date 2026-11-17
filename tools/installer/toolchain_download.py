# SPDX-License-Identifier: GPL-3.0-only
"""Install pinned Linux tools once, independently of the read-only AppImage."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import ssl
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request


def descriptor(runtime):
    path = Path(runtime)/'toolchain-download.json'
    if not path.exists() and not path.is_symlink():
        return None
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 8192:
        raise ValueError('Invalid toolchain download descriptor')
    record = json.loads(path.read_text(encoding='utf-8'))
    if (not isinstance(record, dict) or type(record.get('version')) is not int or record['version'] != 1
            or record.get('platform') != 'linux-x86_64'
            or any(not isinstance(record.get(key), str) or not re.fullmatch('[a-f0-9]{64}', record[key])
                   for key in ('sha256', 'image_id'))
            or type(record.get('size')) is not int or not 0 < record['size'] <= 2*1024**3
            or type(record.get('unpacked_size')) is not int or not 0 < record['unpacked_size'] <= 8*1024**3):
        raise ValueError('Invalid toolchain download descriptor')
    url = urllib.parse.urlsplit(record.get('url', '') if isinstance(record.get('url'), str) else '')
    if url.scheme != 'https' or not url.hostname or url.username or url.password or url.fragment:
        raise ValueError('Toolchain download requires an HTTPS URL without credentials')
    return record


def cache_path(record):
    data = Path(os.environ.get('XDG_DATA_HOME') or Path.home()/'.local/share').absolute()
    parent = data/'kirby-native/toolchains'
    for path in (data/'kirby-native', parent, parent/record['sha256']):
        if path.is_symlink():
            raise ValueError('Toolchain cache must not be a symlink')
    return parent/record['sha256']


def cached_toolchain(runtime):
    record = descriptor(runtime)
    if record is None:
        return None
    target = cache_path(record)
    if not target.is_dir():
        raise ValueError('Build tools are not installed; run setup to download them')
    return target, record['image_id']


def _download(record, output, job, runtime):
    digest = hashlib.sha256()
    completed = 0
    request = urllib.request.Request(record['url'], headers={'User-Agent': 'Kirby-native-setup'})
    certificates = Path(runtime)/'ca-certificates.crt'
    context = ssl.create_default_context(cafile=str(certificates) if certificates.is_file() else None)
    with urllib.request.urlopen(request, timeout=10, context=context) as response, output.open('xb') as stream:
        if urllib.parse.urlsplit(response.geturl()).scheme != 'https':
            raise ValueError('Toolchain download redirected away from HTTPS')
        while chunk := response.read(256*1024):
            job.check_cancel()
            completed += len(chunk)
            if completed > record['size']:
                raise ValueError('Toolchain download size exceeds its pin')
            digest.update(chunk)
            stream.write(chunk)
            job.emit(dict(stage='prepare', state='progress', completed=completed, total=record['size'],
                          detail='Downloading installation tools'))
    job.check_cancel()
    if completed != record['size']:
        raise ValueError('Toolchain download size does not match its pin; retry setup')
    if digest.hexdigest() != record['sha256']:
        raise ValueError('Toolchain download checksum mismatch; retry setup')


def extract(archive_path, destination, maximum, job):
    """Accept regular files, directories and relative links inside an empty tree."""
    destination = Path(destination).resolve()
    total = 0
    reported = 0
    entries = 0
    try:
        with tarfile.open(archive_path, mode='r|xz') as archive:
            for member in archive:
                job.check_cancel()
                entries += 1
                name = PurePosixPath(member.name)
                if (entries > 200000 or name.is_absolute() or '..' in name.parts
                        or not name.parts or '\\' in member.name):
                    raise ValueError('Unsafe toolchain archive path')
                target = destination.joinpath(*name.parts)
                if not target.parent.resolve().is_relative_to(destination):
                    raise ValueError('Toolchain archive path escapes through a link')
                if member.isdir():
                    if target.is_symlink():
                        raise ValueError('Toolchain directory is a link')
                    target.mkdir(parents=True, exist_ok=True)
                elif member.issym():
                    link = PurePosixPath(member.linkname)
                    resolved = posixpath.normpath(posixpath.join(str(name.parent), member.linkname))
                    if (link.is_absolute() or '\\' in member.linkname or resolved == '..'
                            or resolved.startswith('../') or not member.linkname):
                        raise ValueError('Unsafe toolchain archive link')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.symlink_to(member.linkname)
                elif member.isfile():
                    total += member.size
                    if member.size < 0 or total > maximum:
                        raise ValueError('Toolchain unpacked size exceeds its pin')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(member) as source, target.open('xb') as output:
                        while chunk := source.read(256*1024):
                            job.check_cancel()
                            output.write(chunk)
                    if total-reported >= 4*1024*1024:
                        job.emit(dict(stage='prepare', state='progress', completed=total, total=maximum,
                                      detail='Unpacking installation tools'))
                        reported = total
                    target.chmod(member.mode & 0o777)
                else:
                    raise ValueError('Unsupported toolchain archive entry')
    except (tarfile.TarError, EOFError) as error:
        raise ValueError('Invalid toolchain archive') from error
    if total != maximum:
        raise ValueError('Toolchain unpacked size does not match its pin')
    # Forward links must also resolve inside the completed tree.
    for directory, directories, files in os.walk(destination, followlinks=False):
        job.check_cancel()
        for name in (*directories, *files):
            path = Path(directory)/name
            if path.is_symlink() and not path.resolve().is_relative_to(destination):
                raise ValueError('Toolchain archive link escapes the completed tree')


def ensure_toolchain(root, job):
    from .portable import PortableTools
    runtime = Path(root)/'tools/runtime'
    record = descriptor(runtime)
    if record is None:
        return PortableTools.find(root)
    target = cache_path(record)
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    # Different application versions have separate setup locks but share tools.
    import fcntl
    lock_path = target.parent/(record['sha256']+'.lock')
    if lock_path.is_symlink():
        raise ValueError('Toolchain lock must not be a symlink')
    with lock_path.open('a+b') as lock:
        while True:
            job.check_cancel()
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                time.sleep(0.1)
        if target.exists():
            return PortableTools.find(root)
        job.emit(dict(stage='prepare', state='started', completed=0, total=record['size'],
                      detail='Downloading installation tools'))
        with tempfile.TemporaryDirectory(prefix='.download-', dir=target.parent) as temporary:
            staging = Path(temporary)
            archive = staging/'tools.tar.xz'
            _download(record, archive, job, runtime)
            unpacked = staging/'tools'
            unpacked.mkdir()
            job.emit(dict(stage='prepare', state='started', completed=0, total=record['unpacked_size'],
                          detail='Unpacking installation tools'))
            extract(archive, unpacked, record['unpacked_size'], job)
            job.emit(dict(stage='prepare', state='started', detail='Checking installation tools'))
            tools = PortableTools.from_base(root, unpacked)
            if tools is None:
                raise ValueError('Downloaded toolchain manifest is missing')
            if tools.image_id != record['image_id']:
                raise ValueError('Downloaded toolchain identity mismatch')
            job.check_cancel()
            os.rename(unpacked, target)
        job.emit(dict(stage='prepare', state='complete', completed=record['size'], total=record['size']))
    return PortableTools.find(root)
