#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify reusable Linux tools or prepare tools for an existing game release."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlsplit
from urllib.request import urlopen

from installer.toolchain_download import descriptor as validate_descriptor
from package_portable import export_tools
from package_toolchain import build as archive_toolchain

ROOT = Path(__file__).resolve().parents[1]
PIN = 'packaging/linux/toolchain-release.json'
ARCHIVE = 'Kirby-tools-linux-x86_64.tar.xz'
# Older releases name installers Kirby-Setup-linux-x86_64.AppImage; newer ones carry their
# version after Kirby-Setup- (Kirby-Setup-0.1.5-linux-x86_64.AppImage). Both count.
INSTALLER_ASSET = re.compile(r'Kirby-Setup-([0-9A-Za-z._-]+-)?'
                             r'(linux-x86_64\.(AppImage|tar\.gz)|windows-x86_64\.(zip|tar\.gz)|windows-x64\.zip)')
INPUT_FILES = ('tools/Containerfile', 'tools/Containerfile.portable',
               'tools/package_toolchain.py', 'tools/installer/portable.py')
EXPORT_FUNCTIONS = ('extract_rootfs', 'digest', 'export_tools')


def translator_revision(root):
    return json.loads((Path(root)/'dependencies.json').read_text())['wiicompiled']['commit']


def fingerprint(root):
    root = Path(root)
    inputs = {name: (root/name).read_text() for name in INPUT_FILES}
    source = (root/'tools/package_portable.py').read_text()
    tree = ast.parse(source)
    # ast.dump changes between Python releases; source segments have a stable identity.
    functions = {node.name: ast.get_source_segment(source, node)
                 for node in tree.body if isinstance(node, ast.FunctionDef)}
    for name in EXPORT_FUNCTIONS:
        inputs['export:'+name] = functions[name]
    inputs['translator_revision'] = translator_revision(root)
    return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()


def https_url(url):
    parsed = urlsplit(url)
    if (parsed.scheme != 'https' or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.fragment):
        raise ValueError('Tools descriptor URL must use HTTPS without credentials or fragments')
    return url


def fetch_descriptor(url):
    with urlopen(url, timeout=60) as response:
        https_url(response.geturl())
        return response.read(8193)


def read_record(body):
    if len(body) > 8192:
        raise ValueError('Tools descriptor is too large')
    with tempfile.TemporaryDirectory(prefix='kirby-tools-descriptor-') as temporary:
        path = Path(temporary)/'toolchain-download.json'
        path.write_bytes(body)
        return validate_descriptor(path.parent)


def check_pin(pin):
    if pin.get('version') != 1:
        raise ValueError('Unsupported tools release pin version')
    https_url(pin['descriptor_url'])
    for key in ('descriptor_sha256', 'inputs_sha256'):
        if not re.fullmatch(r'[0-9a-f]{64}', pin.get(key, '')):
            raise ValueError('Invalid tools release pin '+key)


def resolve(root, pin, output, fetch=fetch_descriptor):
    check_pin(pin)
    if fingerprint(root) != pin['inputs_sha256']:
        raise ValueError('Linux tools inputs changed: run Update Linux tools, then review and update '+PIN)
    body = fetch(pin['descriptor_url'])
    if len(body) > 8192:
        raise ValueError('Tools descriptor is too large')
    if hashlib.sha256(body).hexdigest() != pin['descriptor_sha256']:
        raise ValueError('Tools descriptor SHA-256 mismatch')
    record = read_record(body)
    if record.get('translator_revision') != translator_revision(root):
        raise ValueError('Tools descriptor does not match the pinned translator revision')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(body)


def validate_tag(tag):
    if (not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', tag)
            or '..' in tag or tag.endswith(('.', '.lock')) or tag.startswith('tools-linux-')):
        raise ValueError('Choose an existing game release tag, such as v0.2.0-alpha.1, using letters, digits, dots, dashes or underscores')


def tools_archive_name(asset_id=None):
    if asset_id is None:
        return ARCHIVE
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', asset_id):
        raise ValueError('Tools asset identifier must contain letters, digits, dashes or underscores')
    return f'Kirby-tools-linux-x86_64-{asset_id}.tar.xz'


def archive_url(repository, tag, asset_id=None):
    validate_tag(tag)
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*/[A-Za-z0-9_][A-Za-z0-9_.-]*', repository):
        raise ValueError('Use an OWNER/REPO repository name')
    return f'https://github.com/{repository}/releases/download/{tag}/{tools_archive_name(asset_id)}'


def validate_release(record, tag, asset_id=None):
    """Require a game release with space for tools without replacing pinned assets."""
    validate_tag(tag)
    if record.get('tag_name') != tag:
        raise ValueError('Destination release does not match the selected game tag')
    if record.get('immutable'):
        raise ValueError('Destination release is immutable; select a mutable game release')
    assets = {asset['name'] for asset in record.get('assets', [])}
    if not any(INSTALLER_ASSET.fullmatch(name) for name in assets):
        raise ValueError('Destination must already contain a game installer; wait for Release installers to finish')
    archive = tools_archive_name(asset_id)
    names = (archive, archive+'.sha256', archive+'.json', archive+'.json.sha256')
    if assets.intersection(names):
        raise ValueError('Destination already contains this tools version; start a new workflow run to preserve existing pinned downloads')


def make_pin(root, archive, descriptor_url):
    https_url(descriptor_url)
    archive = Path(archive)
    body = archive.with_name(archive.name+'.json').read_bytes()
    record = read_record(body)
    with archive.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    if record['sha256'] != checksum or record['size'] != archive.stat().st_size:
        raise ValueError('Tools archive does not match its descriptor')
    if descriptor_url != record['url']+'.json':
        raise ValueError('Descriptor URL does not match the tools archive URL')
    if record.get('translator_revision') != translator_revision(root):
        raise ValueError('Tools descriptor does not match the pinned translator revision')
    return dict(version=1, descriptor_url=descriptor_url,
                descriptor_sha256=hashlib.sha256(body).hexdigest(), inputs_sha256=fingerprint(root))


def checksum_file(path):
    with path.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    path.with_name(path.name+'.sha256').write_text(f'{checksum}  {path.name}\n')


def build(root, output, image, engine, repository, tag, asset_id=None):
    url = archive_url(repository, tag, asset_id)
    output = Path(output)
    if output.exists():
        raise ValueError('Tools output directory already exists; choose a new path')
    image_id = subprocess.check_output([engine, 'image', 'inspect', image, '--format', '{{.Id}}'], text=True).strip()
    output.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix='.toolchain-', dir=output.parent) as temporary:
        runtime = Path(temporary)/'runtime'
        export_tools(image_id, engine, runtime)
        archive = output/tools_archive_name(asset_id)
        archive_toolchain(runtime, runtime/'rootfs/opt/kirby-translator', archive, url, translator_revision(root))
        pin = make_pin(root, archive, url+'.json')
        for path in (archive, archive.with_name(archive.name+'.json')):
            checksum_file(path)
        (output/'toolchain-release.json').write_text(json.dumps(pin, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    resolve_parser = commands.add_parser('resolve', help='Download and validate the pinned descriptor')
    resolve_parser.add_argument('--output', type=Path, required=True)
    commands.add_parser('changed', help='Print true when tools inputs differ from the published pin')
    build_parser = commands.add_parser('build', help='Export tools and write a proposed replacement pin')
    build_parser.add_argument('--output', type=Path, required=True)
    build_parser.add_argument('--image', default='localhost/kirby-portable-tools:compact')
    build_parser.add_argument('--engine', choices=('docker', 'podman'), default='docker')
    build_parser.add_argument('--repository', required=True)
    build_parser.add_argument('--tag', required=True)
    build_parser.add_argument('--asset-id', help='Unique tools asset version; workflow runs use their run ID')
    check_parser = commands.add_parser('check-release', help='Validate existing game release metadata before attaching tools')
    check_parser.add_argument('--tag', required=True)
    check_parser.add_argument('--asset-id')
    check_parser.add_argument('--release-json', type=Path, required=True)
    url_parser = commands.add_parser('archive-url', help='Validate a game release tag and return its future tools asset URL')
    url_parser.add_argument('--repository', required=True)
    url_parser.add_argument('--tag', required=True)
    url_parser.add_argument('--asset-id')
    args = parser.parse_args()
    if args.command in ('resolve', 'changed'):
        pin = json.loads((ROOT/PIN).read_text())
        check_pin(pin)
        if args.command == 'changed':
            print(str(fingerprint(ROOT) != pin['inputs_sha256']).lower())
        else:
            resolve(ROOT, pin, args.output)
    elif args.command == 'check-release':
        validate_release(json.loads(args.release_json.read_text()), args.tag, args.asset_id)
        print('Game release destination verified; existing assets will be preserved')
    elif args.command == 'archive-url':
        print(archive_url(args.repository, args.tag, args.asset_id))
    else:
        build(ROOT, args.output, args.image, args.engine, args.repository, args.tag, args.asset_id)


if __name__ == '__main__':
    main()
