#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a local-use .app from a Mac build; never publish generated game code."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from macos.toolchain import macos_architecture, macos_options

ROOT = Path(__file__).resolve().parents[2]


def check_macho(path, arch):
    if arch not in ('x86_64', 'arm64'):
        raise ValueError('Unknown Mach-O architecture')
    with Path(path).open('rb') as stream:
        header = stream.read(32)
    # Separate thin binaries are intentional. Reject ELF, PE, fat archives,
    # dylibs and stale products built for the other CPU.
    cpu = 0x01000007 if arch == 'x86_64' else 0x0100000c
    if len(header) != 32 or struct.unpack_from('<I', header)[0] != 0xfeedfacf or \
            struct.unpack_from('<I', header, 4)[0] != cpu or struct.unpack_from('<I', header, 12)[0] != 2:
        raise ValueError(f'Expected a macOS {arch} Mach-O executable: {path}')


def package(root, arch, qt_prefix, *, output=None, deployment_target='13.0'):
    arch = macos_architecture(arch)
    macos_options(arch, deployment_target)
    root, qt_prefix = Path(root).resolve(), Path(qt_prefix).resolve()
    build = root/f'build/macos-{arch}'
    output = Path(output).absolute() if output else root/f'local/releases/Kirby-macOS-{arch}.app'
    if output.suffix != '.app' or output.exists() or output.is_symlink():
        raise ValueError('Choose a new .app destination; existing apps and saves are left untouched')
    required = ('kirby-launcher', 'WiiCompiled', 'qt-launcher/kirby-ui', 'dsp_coef.bin')
    for name in required:
        path = build/name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Missing regular build output: {path}')
        if name != 'dsp_coef.bin':
            check_macho(path, arch)
    deploy = qt_prefix/'bin/macdeployqt'
    if not deploy.is_file():
        raise ValueError(f'macdeployqt is missing from the Qt kit: {deploy}')
    offscreen = qt_prefix/'plugins/platforms/libqoffscreen.dylib'
    if not offscreen.is_file():
        raise ValueError(f'The Qt kit is missing the launcher self-test plugin: {offscreen}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kirby-app-', dir=output.parent) as temporary:
        app = Path(temporary)/output.name
        contents = app/'Contents'; binary = contents/'MacOS'; resources = contents/'Resources'
        binary.mkdir(parents=True); resources.mkdir()
        for name in required:
            destination = binary/("kirby-ui" if name == "qt-launcher/kirby-ui" else name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(build/name, destination)
        (binary/'kirby-macos-bundle.txt').write_text('Kirby macOS bundle v1\n')
        info = dict(CFBundleName="Kirby's Epic Yarn", CFBundleDisplayName="Kirby's Epic Yarn",
                    CFBundleExecutable='kirby-launcher', CFBundleIdentifier='dev.portinglab.kirby',
                    CFBundlePackageType='APPL', CFBundleVersion='1', CFBundleShortVersionString='0.1.0',
                    LSMinimumSystemVersion=deployment_target, NSHighResolutionCapable=True)
        with (contents/'Info.plist').open('wb') as stream:
            plistlib.dump(info, stream)
        for name in ('LICENSE', 'THIRD_PARTY.md', 'dependencies.json'):
            shutil.copy2(root/name, resources/name)
        # Preserve third-party notices without copying any game data or caches.
        from windows.package_windows import license_files
        for directory in (root/'licenses', root/'vendor', build/'_deps'):
            for notice in license_files(directory):
                destination=resources/'licenses'/notice.relative_to(root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice,destination)
        # macdeployqt selects Cocoa by default; our diagnostic self-test also
        # needs offscreen. Include it explicitly and relocate its dependencies.
        bundled_offscreen=contents/'PlugIns/platforms/libqoffscreen.dylib'
        bundled_offscreen.parent.mkdir(parents=True)
        shutil.copy2(offscreen,bundled_offscreen)
        subprocess.run([deploy, app, '-always-overwrite',
                        f'-executable={binary / "kirby-ui"}',
                        f'-executable={binary / "WiiCompiled"}',
                        f'-executable={bundled_offscreen}'], check=True)
        (binary/'qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=PlugIns\nLibraries=Frameworks\nTranslations=Resources/translations\n')
        translations=resources/'translations'; translations.mkdir(exist_ok=True)
        for language in ('es', 'zh_CN'):
            source=qt_prefix/f'translations/qtbase_{language}.qm'
            if source.is_file(): shutil.copy2(source, translations/source.name)
        # Local ad-hoc signatures are needed after modifying Mach-O load paths.
        # This is not Developer ID signing or notarization.
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', app], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', app], check=True)
        clean={key:value for key,value in os.environ.items()
               if not key.startswith(('DYLD_', 'QT_', 'QML', 'LD_LIBRARY_PATH'))}
        clean.update(QT_QPA_PLATFORM='offscreen', PATH='/usr/bin:/bin:/usr/sbin:/sbin')
        # Exercise the relocated frontend directly: self-test must not create a
        # user's Application Support directory merely to test a package.
        for name, argument in (('WiiCompiled','--build-info'), ('kirby-ui','--self-test')):
            subprocess.run([binary/name,argument],cwd=temporary,env=clean,check=True,timeout=90)
        app.rename(output)
    print(f'Local-use app ready: {output}\nKeep this compiled game private; release installers must build from each user\'s own disc.')
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch',choices=('x86_64','arm64'))
    parser.add_argument('--qt-prefix',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--deployment-target',default='13.0')
    args=parser.parse_args()
    try:
        package(ROOT,args.arch,args.qt_prefix,output=args.output,deployment_target=args.deployment_target)
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        print(f'ERROR: {error}',file=sys.stderr);return 1
    return 0


if __name__=='__main__':
    sys.exit(main())
