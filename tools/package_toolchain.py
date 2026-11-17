#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Archive reusable Linux build tools without SDKs or packaging-only files."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile

from installer.portable import PortableTools
from installer.toolchain_download import descriptor as validate_descriptor

EXCLUDED = ('rootfs/opt/freeze', 'rootfs/usr/share/powershell',
            'rootfs/usr/share/dotnet/sdk', 'rootfs/usr/share/dotnet/sdk-manifests',
            'rootfs/usr/share/dotnet/packs', 'rootfs/usr/share/dotnet/templates',
            'rootfs/root/.nuget', 'rootfs/root/.cache', 'rootfs/var/cache')


def included(name):
    return ('.git' not in Path(name).parts
            and not any(name == prefix or name.startswith(prefix+'/') for prefix in EXCLUDED))


def build(runtime, translator, output, url, translator_revision):
    runtime, translator, output = Path(runtime), Path(translator), Path(output)
    if output.exists():
        raise ValueError('Toolchain output already exists')
    if (not (translator/'Translator.Cli.dll').is_file()
            or (translator/'source-revision').read_text().strip() != translator_revision):
        raise ValueError('Prebuilt translator does not match the pinned source revision')
    tools = PortableTools.from_base(runtime, runtime)
    if tools is None:
        raise ValueError('Prepared toolchain manifest is missing')
    record = json.loads((runtime/'toolchain.json').read_text())
    record['translator_revision'] = translator_revision
    record['files'] = dict(record['files'])
    # Validate the URL before spending time compressing the real payload.
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_descriptor = dict(version=1, platform='linux-x86_64', image_id=tools.image_id,
                                url=url, sha256='0'*64, size=1, unpacked_size=1,
                                translator_revision=translator_revision)
    import tempfile
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        path = Path(temporary)/'toolchain-download.json'
        path.write_text(json.dumps(temporary_descriptor))
        validate_descriptor(path.parent)
    total = 0
    try:
        with tarfile.open(output, mode='w:xz', preset=3) as archive:
            def add(path, name):
                nonlocal total
                # Store each regular file directly, avoiding tar hard-link entries.
                archive.inodes.clear()
                entry = archive.gettarinfo(str(path), arcname=name)
                entry.uid = entry.gid = 0
                entry.uname = entry.gname = 'root'
                entry.mtime = 0  # Identical tools must share one cache across app versions.
                if entry.isfile():
                    total += entry.size
                    with path.open('rb') as stream:
                        archive.addfile(entry, stream)
                else:
                    archive.addfile(entry)

            def walk(path, name):
                if not included(name):
                    return
                add(path, name)
                if path.is_dir() and not path.is_symlink():
                    for child in sorted(path.iterdir()):
                        walk(child, name+'/'+child.name)

            for name in ('rootfs', 'proot', 'lib'):
                if (runtime/name).exists():
                    walk(runtime/name, name)
            # Container builds already put this here. Overlay older prepared runtimes too.
            if translator.resolve() != (runtime/'rootfs/opt/kirby-translator').resolve():
                walk(translator, 'rootfs/opt/kirby-translator')
            for path in sorted(translator.rglob('*')):
                if path.is_file():
                    name = 'rootfs/opt/kirby-translator/'+path.relative_to(translator).as_posix()
                    record['files'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
            document = (json.dumps(record, indent=2)+'\n').encode()
            entry = tarfile.TarInfo('toolchain.json'); entry.size = len(document); entry.mode = 0o644
            archive.addfile(entry, io.BytesIO(document)); total += len(document)
        with output.open('rb') as stream:
            checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
        result = dict(temporary_descriptor, sha256=checksum, size=output.stat().st_size, unpacked_size=total)
        output.with_name(output.name+'.json').write_text(json.dumps(result, indent=2)+'\n')
        return result
    except BaseException:
        output.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--translator', type=Path, required=True)
    parser.add_argument('--translator-revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.runtime, args.translator, args.output, args.url, args.translator_revision), indent=2))


if __name__ == '__main__':
    main()
