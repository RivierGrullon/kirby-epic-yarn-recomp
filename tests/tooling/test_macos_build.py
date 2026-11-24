# SPDX-License-Identifier: GPL-3.0-only
import os
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from macos import toolchain as apple
from macos import package_macos as bundle
from macos import build_macos
import build_native


def executable(path, arch='x86_64', kind=2):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack('<8I', 0xfeedfacf, 0x01000007 if arch=='x86_64' else 0x0100000c,
                                 3, kind, 0, 0, 0, 0))
    path.chmod(0o755)


class MacBuildTests(unittest.TestCase):
    def test_native_architecture_and_rosetta_guards(self):
        for arch in ('x86_64', 'arm64'):
            with self.subTest(arch=arch), patch.object(apple.sys,'platform','darwin'), \
                 patch.object(apple.platform,'machine',return_value=arch), \
                 patch.object(apple.subprocess,'run',return_value=subprocess.CompletedProcess([],0,stdout='0\n')):
                self.assertEqual(apple.macos_architecture(),arch)
                self.assertEqual(apple.macos_architecture(arch),arch)
                with self.assertRaisesRegex(ValueError,'matching native host'):
                    apple.macos_architecture('arm64' if arch=='x86_64' else 'x86_64')
        with patch.object(apple.sys,'platform','darwin'), patch.object(apple.platform,'machine',return_value='x86_64'), \
             patch.object(apple.subprocess,'run',return_value=subprocess.CompletedProcess([],0,stdout='1\n')):
            with self.assertRaisesRegex(ValueError,'Rosetta'): apple.macos_architecture()
        with patch.object(apple.sys,'platform','linux'):
            with self.assertRaisesRegex(ValueError,'require a Mac'): apple.macos_architecture()

    def test_target_options_reject_unsupported_or_injected_versions(self):
        for invalid in ('12.0','13','13.0;-DBAD=ON','latest'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                apple.macos_options('arm64',invalid)
        self.assertIn('-DCMAKE_OSX_ARCHITECTURES=arm64',apple.macos_options('arm64','13.0'))
        self.assertIn('-DMKW_X86_CPU_PROFILE=v2',apple.macos_options('x86_64','13.0'))

    def test_reject_wrong_cpu_and_nonexecutables(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'binary'
            for arch in ('arm64','x86_64'):
                executable(path,arch);bundle.check_macho(path,arch)
                with self.assertRaises(ValueError): bundle.check_macho(path,'arm64' if arch=='x86_64' else 'x86_64')
                executable(path,arch,kind=6)
                with self.assertRaises(ValueError): bundle.check_macho(path,arch)
            for data in (b'',b'\x7fELF'+bytes(60),b'MZ'+bytes(62)):
                path.write_bytes(data)
                with self.assertRaises(ValueError): bundle.check_macho(path,'x86_64')

    def test_package_is_allowlisted_and_preserves_existing_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);build=root/'build/macos-arm64';qt=root/'qt'
            for name in ('kirby-launcher','WiiCompiled','qt-launcher/kirby-ui'): executable(build/name,'arm64')
            (build/'dsp_coef.bin').write_bytes(b'fixture')
            (build/'save.bin').write_text('must not be packaged')
            (root/'disc').mkdir();(root/'disc/main.dol').write_text('private')
            (qt/'bin').mkdir(parents=True);(qt/'bin/macdeployqt').touch()
            (qt/'plugins/platforms').mkdir(parents=True);(qt/'plugins/platforms/libqoffscreen.dylib').touch()
            for name in ('LICENSE','THIRD_PARTY.md','dependencies.json'): (root/name).write_text('fixture')
            with patch.object(bundle,'macos_architecture',return_value='arm64'), patch.object(bundle.subprocess,'run') as run:
                output=bundle.package(root,'arm64',qt)
                info=plistlib.loads((output/'Contents/Info.plist').read_bytes())
                self.assertEqual(info['CFBundleExecutable'],'kirby-launcher')
                self.assertTrue((output/'Contents/MacOS/qt.conf').is_file())
                self.assertTrue((output/'Contents/PlugIns/platforms/libqoffscreen.dylib').is_file())
                names={p.name for p in output.rglob('*')}
                self.assertNotIn('main.dol',names);self.assertNotIn('save.bin',names)
                with self.assertRaisesRegex(ValueError,'existing apps'):
                    bundle.package(root,'arm64',qt)
                self.assertTrue(output.is_dir())
                self.assertTrue(any('--verify' in list(call.args[0]) for call in run.call_args_list))

    def test_package_failure_does_not_publish_partial_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);build=root/'build/macos-x86_64';qt=root/'qt'
            for name in ('kirby-launcher','WiiCompiled','qt-launcher/kirby-ui'): executable(build/name)
            (build/'dsp_coef.bin').touch();(qt/'bin').mkdir(parents=True);(qt/'bin/macdeployqt').touch()
            (qt/'plugins/platforms').mkdir(parents=True);(qt/'plugins/platforms/libqoffscreen.dylib').touch()
            for name in ('LICENSE','THIRD_PARTY.md','dependencies.json'): (root/name).touch()
            with patch.object(bundle,'macos_architecture',return_value='x86_64'), \
                 patch.object(bundle.subprocess,'run',side_effect=subprocess.CalledProcessError(1,'macdeployqt')):
                with self.assertRaises(subprocess.CalledProcessError): bundle.package(root,'x86_64',qt)
            self.assertFalse((root/'local/releases/Kirby-macOS-x86_64.app').exists())

    @unittest.skipIf(os.name == 'nt', 'Mac source layout uses POSIX symlinks')
    def test_two_native_targets_keep_separate_cmake_caches(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for name in ('generated/build_shards/shards.cmake','generated/KirbyBootConfig.h',
                         'vendor/wiicompiled/runtime/CMakeLists.txt','vendor/wiicompiled/aurora-main/CMakeLists.txt',
                         'vendor/wiicompiled/LICENSE'):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('fixture')
            (root/'dependencies.json').write_text(json.dumps({'wiicompiled':{
                'commit':'a'*40,'submodules':{'aurora-main':'b'*40}}}))
            old=root/'build/native/WiiCompiled';old.parent.mkdir(parents=True);old.write_text('previous Linux build')
            for arch in ('x86_64','arm64'):
                with patch.object(build_native,'ROOT',root), patch.object(apple,'macos_architecture',return_value=arch), \
                     patch.object(sys,'argv',['build_native.py','--target','macos','--macos-arch',arch,'--configure-only']), \
                     patch.object(build_native,'run_logged') as run:
                    self.assertEqual(build_native.main(),0)
                    arguments=list(map(str,run.call_args.args[1]))
                    self.assertEqual(arguments[arguments.index('-B')+1],str(root.resolve()/f'build/macos-{arch}'))
                    self.assertIn('-DCMAKE_OSX_ARCHITECTURES='+arch,arguments)
                    self.assertIn('-DCMAKE_CXX_COMPILER=/usr/bin/clang++',arguments)
                    self.assertEqual((root/f'local/native-workspace-macos-{arch}/generated').resolve(),(root/'generated').resolve())
            self.assertEqual(old.read_text(),'previous Linux build')
