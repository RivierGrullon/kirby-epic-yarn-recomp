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


def make_appdir(root, output, image, engine):
    root, output = Path(root).resolve(), Path(output).absolute()
    if output.exists():
        raise ValueError('Package output already exists; choose a new path')
    # Never include a dirty source snapshot or a launcher built from another tree.
    from build_info import collect
    expected = collect(root,compiler='pending',build_type='Release')
    if expected['source_modified']:
        raise ValueError('Commit project changes before packaging')
    image = subprocess.check_output([engine,'image','inspect',image,'--format','{{.Id}}'],text=True).strip().removeprefix('sha256:')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.appdir-',dir=output.parent) as temporary:
        appdir=Path(temporary)/'Kirby.AppDir';appdir.mkdir()
        project=appdir/'project';project_snapshot(root,project)
        runtime=appdir/'tools/runtime';runtime.mkdir(parents=True)
        identifier='kirby-package-'+uuid.uuid4().hex
        subprocess.run([engine,'create','--name',identifier,image],check=True,stdout=subprocess.DEVNULL)
        try:
            archive_path=Path(temporary)/'rootfs.tar'
            subprocess.run([engine,'export','--output',archive_path,identifier],check=True)
            with tarfile.open(archive_path) as archive:
                extract_rootfs(archive,runtime/'rootfs')
        finally:
            subprocess.run([engine,'rm',identifier],check=True,stdout=subprocess.DEVNULL)
        shutil.copy2(runtime/'rootfs/usr/bin/proot',runtime/'proot')
        (runtime/'lib').mkdir()
        for library in (runtime/'rootfs/usr/lib/x86_64-linux-gnu').glob('libtalloc.so*'):
            shutil.copy2(library.resolve(),runtime/'lib'/library.name)
        freeze=root/'local/portable-freeze';freeze.mkdir(parents=True,exist_ok=True)
        container(engine,image,['/opt/freeze/bin/pyinstaller','--noconfirm','--clean',
                  '--distpath','local/portable-freeze/dist','--workpath','local/portable-freeze/work',
                  '--specpath','local/portable-freeze','--name','kirby-setup-worker',
                  '--contents-directory','python','tools/install.py'],root)
        built=freeze/'dist/kirby-setup-worker'
        shutil.copytree(built,runtime,dirs_exist_ok=True)
        files={name:digest(runtime/name) for name in PortableTools.REQUIRED}
        for library in (runtime/'lib').iterdir():
            files[library.relative_to(runtime).as_posix()]=digest(library)
        (runtime/'toolchain.json').write_text(json.dumps(dict(version=1,platform='linux-x86_64',
            image_id=image,files=files),indent=2)+'\n')
        launcher=root/'build/native/kirby-launcher'
        environment=dict(os.environ,LD_LIBRARY_PATH=str(root/'local/host-libs'))
        import tomllib
        information=tomllib.loads(subprocess.check_output([launcher,'--build-info'],env=environment,text=True))
        for key in ('source_revision','source_modified','source_sha256'):
            if information['build'][key] != expected[key]:
                raise ValueError('Rebuild the launcher from the committed package sources')
        (project/'build/native').mkdir(parents=True)
        shutil.copy2(launcher,project/'build/native/kirby-launcher')
        if (root/'local/host-libs').is_dir():
            shutil.copytree(root/'local/host-libs',appdir/'host-libs',symlinks=False)
        else:
            (appdir/'host-libs').mkdir()
        artwork = root/'packaging/linux'
        for name in ('AppRun', 'kirby-native.desktop'):
            shutil.copy2(artwork/name, appdir/name)
        theme = Path('usr/share/icons/hicolor')
        png_icon = theme/'512x512/apps/kirby-native.png'
        svg_icon = theme/'scalable/apps/kirby-native.svg'
        (appdir/png_icon).parent.mkdir(parents=True)
        (appdir/svg_icon).parent.mkdir(parents=True)
        shutil.copy2(artwork/'yarn-mascot-icon.png', appdir/png_icon)
        shutil.copy2(artwork/'kirby-native.svg', appdir/svg_icon)
        (appdir/'kirby-native.png').symlink_to(png_icon)
        (appdir/'.DirIcon').symlink_to('kirby-native.png')
        (appdir/'revision').write_text(expected['source_revision']+'\n')
        # The rootfs retains /usr/share/doc and Dolphin's corresponding source.
        shutil.copy2(root/'THIRD_PARTY.md',appdir/'THIRD_PARTY.md')
        shutil.copy2(root/'LICENSE',appdir/'LICENSE')
        os.rename(appdir,output)
    print('AppDir ready:',output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',default='localhost/kirby-portable-tools:dev')
    parser.add_argument('--engine',choices=('podman','docker'),default='podman')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    make_appdir(ROOT,args.output,args.image,args.engine)


if __name__ == '__main__':
    main()
