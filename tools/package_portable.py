#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a Linux AppDir containing tools and authored sources, never game code."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import subprocess
import tarfile
import tempfile
import uuid

from installer.portable import PortableTools
from package_toolchain import build as archive_toolchain
from installer.toolchain_download import descriptor as read_descriptor

ROOT = Path(__file__).resolve().parents[1]


def extract_rootfs(archive, destination):
    """Relocate rootfs links without giving archive entries access to the host."""
    destination = Path(destination).absolute()
    destination.mkdir(parents=True, exist_ok=True)
    for entry in archive:
        path = PurePosixPath(entry.name)
        if path.is_absolute() or '..' in path.parts:
            raise ValueError('Unsafe rootfs archive path')
        if not path.parts or path.parts[0] in ('dev', 'proc', 'sys', 'run', 'tmp', 'workspace'):
            continue
        target = destination.joinpath(*path.parts)
        if not target.parent.resolve().is_relative_to(destination):
            raise ValueError('Rootfs archive path escaped through a link')
        if entry.issym():
            link = (posixpath.normpath(entry.linkname).lstrip('/') if entry.linkname.startswith('/') else
                    posixpath.normpath(posixpath.join(str(path.parent), entry.linkname)))
            if link == '..' or link.startswith('../'):
                raise ValueError('Unsafe rootfs archive link')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(posixpath.relpath(link, str(path.parent)))
        elif entry.isdir():
            target.mkdir(parents=True, exist_ok=True)
        elif entry.isfile() or entry.islnk():
            if not target.resolve().is_relative_to(destination):
                raise ValueError('Unsafe rootfs file path')
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(entry) as source, target.open('xb') as output:
                shutil.copyfileobj(source, output)
            target.chmod(entry.mode & 0o777)
    for name in ('dev', 'proc', 'sys', 'tmp', 'run', 'workspace'):
        (destination/name).mkdir(exist_ok=True)


def project_snapshot(root, destination):
    root = Path(root).resolve()
    if subprocess.check_output(['git','-C',root,'status','--porcelain','--untracked-files=all']):
        raise ValueError('Commit project changes before packaging')
    subprocess.run(['git','-c','core.logAllRefUpdates=false','clone','--quiet','--no-local',
                    '--no-hardlinks','--depth','1',root.as_uri(),str(destination)],check=True)
    subprocess.run(['git','-C',destination,'remote','remove','origin'],check=True)
    forbidden = ('disc', 'generated', 'local', 'build')
    if any((Path(destination)/name).exists() for name in forbidden):
        raise ValueError('Release source tree contains private or generated outputs')
    return subprocess.check_output(['git','-C',destination,'rev-parse','HEAD'],text=True).strip()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def container(engine, image, arguments, root=ROOT):
    command = [engine,'run','--rm','--network=host']
    if engine == 'podman':
        command += ['--userns=keep-id','--security-opt','label=disable']
    command += ['-e','HOME=/tmp/kirby-package-home','-v',str(root)+':/workspace:rw',
                '-w','/workspace',image,*map(os.fspath,arguments)]
    subprocess.run(command,check=True)


def export_tools(image, engine, runtime):
    runtime = Path(runtime); runtime.mkdir(parents=True)
    identifier='kirby-package-'+uuid.uuid4().hex
    subprocess.run([engine,'create','--name',identifier,image],check=True,stdout=subprocess.DEVNULL)
    archive_path=runtime.parent/'rootfs.tar'
    try:
        subprocess.run([engine,'export','--output',archive_path,identifier],check=True)
        with tarfile.open(archive_path) as archive:
            extract_rootfs(archive,runtime/'rootfs')
    finally:
        subprocess.run([engine,'rm',identifier],check=True,stdout=subprocess.DEVNULL)
        archive_path.unlink(missing_ok=True)
    shutil.copy2(runtime/'rootfs/usr/bin/proot',runtime/'proot')
    (runtime/'lib').mkdir()
    for library in sorted((runtime/'rootfs/usr/lib/x86_64-linux-gnu').glob('libtalloc.so*')):
        shutil.copy2(library.resolve(),runtime/'lib'/library.name)
    files={name:digest(runtime/name) for name in PortableTools.REQUIRED}
    for library in sorted((runtime/'lib').iterdir()):
        files[library.relative_to(runtime).as_posix()]=digest(library)
    (runtime/'toolchain.json').write_text(json.dumps(dict(version=1,platform='linux-x86_64',
        image_id=image.removeprefix('sha256:'),files=files),indent=2)+'\n')


def make_appdir(root, output, image, engine, tools_output=None, tools_url=None, tools_descriptor=None):
    root, output = Path(root).resolve(), Path(output).absolute()
    if output.exists():
        raise ValueError('Package output already exists; choose a new path')
    image = subprocess.check_output([engine,'image','inspect',image,'--format','{{.Id}}'],text=True).strip().removeprefix('sha256:')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.appdir-',dir=output.parent) as temporary:
        appdir=Path(temporary)/'Kirby.AppDir';appdir.mkdir()
        revision=project_snapshot(root,appdir/'project')
        worker=appdir/'tools/runtime';worker.mkdir(parents=True)
        translator_revision=json.loads((root/'dependencies.json').read_text())['wiicompiled']['commit']
        if tools_descriptor is not None:
            shutil.copy2(tools_descriptor,worker/'toolchain-download.json')
            descriptor=read_descriptor(worker)
            if descriptor is None or descriptor.get('translator_revision') != translator_revision:
                raise ValueError('Toolchain descriptor does not match the pinned translator revision')
        else:
            runtime=Path(temporary)/'toolchain'
            export_tools(image,engine,runtime)
            descriptor=archive_toolchain(runtime,runtime/'rootfs/opt/kirby-translator',
                                         tools_output,tools_url,translator_revision)
            (worker/'toolchain-download.json').write_text(json.dumps(descriptor,indent=2)+'\n')
        freeze=root/'local/portable-freeze';freeze.mkdir(parents=True,exist_ok=True)
        container(engine,image,['/opt/freeze/bin/pyinstaller','--noconfirm','--clean',
                  '--distpath','local/portable-freeze/dist','--workpath','local/portable-freeze/work',
                  '--specpath','local/portable-freeze','--name','kirby-setup-worker',
                  '--exclude-module','PyQt6','--contents-directory','python','tools/install.py'],root)
        container(engine,image,['cp','/etc/ssl/certs/ca-certificates.crt',
                  'local/portable-freeze/dist/kirby-setup-worker/ca-certificates.crt'],root)
        built=freeze/'dist/kirby-setup-worker'
        shutil.copytree(built,worker,dirs_exist_ok=True)
        container(engine,image,['/opt/freeze/bin/pyinstaller','--noconfirm','--clean','--windowed',
                  '--distpath','local/portable-freeze/dist','--workpath','local/portable-freeze/work',
                  '--specpath','local/portable-freeze','--name','kirby-setup',
                  '--contents-directory','python','tools/setup_gui.py'],root)
        shutil.copytree(freeze/'dist/kirby-setup',appdir/'tools/setup')
        container(engine,image,['/opt/freeze/bin/pyinstaller','--noconfirm','--clean',
                  '--distpath','local/portable-freeze/dist','--workpath','local/portable-freeze/work',
                  '--specpath','local/portable-freeze','--name','Kirby-Bridge',
                  '--exclude-module','PyQt6','--contents-directory','python','tools/launcher_bridge.py'],root)
        shutil.copytree(freeze/'dist/Kirby-Bridge',appdir/'tools/bridge')
        from launcher_profiles import write_profile
        write_profile(root,appdir,revision)
        # Preserve the exact Qt/PyQt wheel notices alongside the frozen components.
        container(engine,image,['/opt/freeze/bin/python','-c',
                  "import importlib.metadata,pathlib,shutil;out=pathlib.Path('local/portable-freeze/licenses');out.mkdir(exist_ok=True);"
                  "[(shutil.copytree(p,out/p.name,dirs_exist_ok=True)) for n in ('PyQt6','PyQt6-Qt6','PyQt6-sip') "
                  "for p in [pathlib.Path(importlib.metadata.distribution(n)._path)] if p.is_dir()]"],root)
        shutil.copytree(freeze/'licenses',appdir/'licenses/qt')
        if (root/'local/host-libs').is_dir():
            shutil.copytree(root/'local/host-libs',appdir/'host-libs',symlinks=False)
        else:
            (appdir/'host-libs').mkdir()
        artwork = root/'packaging/linux'
        for name in ('AppRun', 'Play-Kirby.sh', 'kirby-native.desktop'):
            shutil.copy2(artwork/name, appdir/name)
        (appdir/'Play-Kirby.sh').chmod(0o755)
        theme = Path('usr/share/icons/hicolor')
        png_icon = theme/'512x512/apps/kirby-native.png'
        svg_icon = theme/'scalable/apps/kirby-native.svg'
        (appdir/png_icon).parent.mkdir(parents=True)
        (appdir/svg_icon).parent.mkdir(parents=True)
        shutil.copy2(artwork/'yarn-mascot-icon.png', appdir/png_icon)
        shutil.copy2(artwork/'kirby-native.svg', appdir/svg_icon)
        (appdir/'kirby-native.png').symlink_to(png_icon)
        (appdir/'.DirIcon').symlink_to('kirby-native.png')
        (appdir/'revision').write_text(revision+'\n')
        # The separate tools archive retains component notices and corresponding source.
        shutil.copy2(root/'THIRD_PARTY.md',appdir/'THIRD_PARTY.md')
        shutil.copy2(root/'LICENSE',appdir/'LICENSE')
        os.rename(appdir,output)
    print('AppDir ready:',output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',default='localhost/kirby-setup-tools:pyqt')
    parser.add_argument('--engine',choices=('podman','docker'),default='podman')
    parser.add_argument('--output',type=Path,required=True)
    tools=parser.add_mutually_exclusive_group(required=True)
    tools.add_argument('--tools-output',type=Path,help='Separate tools .tar.xz to publish once')
    tools.add_argument('--tools-descriptor',type=Path,help='Reuse a previously published tools archive descriptor')
    parser.add_argument('--tools-url',help='Immutable HTTPS release URL when making a new tools archive')
    args=parser.parse_args()
    if args.tools_output and not args.tools_url:
        parser.error('--tools-output requires --tools-url')
    make_appdir(ROOT,args.output,args.image,args.engine,args.tools_output,args.tools_url,args.tools_descriptor)


if __name__ == '__main__':
    main()
