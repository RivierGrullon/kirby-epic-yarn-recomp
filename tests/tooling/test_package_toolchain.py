# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import package_toolchain as package
import kirby
from installer.portable import PortableTools


class PackageToolchainTests(unittest.TestCase):
    def test_archive_prunes_build_only_tools_and_git_but_keeps_sources_and_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary); runtime = base/'runtime'; translator = base/'translator'
            files = {name: b'fixture tool' for name in PortableTools.REQUIRED}
            files.update({
                'rootfs/opt/dolphin-source/Source/tool.cpp': b'corresponding source',
                'rootfs/opt/dolphin-source/LICENSE': b'license notice',
                'rootfs/opt/dolphin-source/.git/objects/pack/large': b'unneeded history',
                'rootfs/opt/dolphin-source/Externals/lib/.git': b'gitdir metadata',
                'rootfs/opt/freeze/bin/pyinstaller': b'packaging only',
                'rootfs/usr/share/dotnet/sdk/8.0/compiler': b'build only',
                'rootfs/usr/share/dotnet/packs/ref': b'build only',
                'rootfs/usr/share/dotnet/shared/Microsoft.NETCore.App/8.0/runtime': b'runtime needed',
                'rootfs/usr/share/powershell/pwsh': b'not used on Linux',
                'rootfs/usr/share/doc/component/copyright': b'keep notices',
            })
            for name, body in files.items():
                path = runtime/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(body)
            record = dict(version=1, platform='linux-x86_64', image_id='a'*64,
                          files={name: hashlib.sha256(files[name]).hexdigest() for name in PortableTools.REQUIRED})
            (runtime/'toolchain.json').write_text(json.dumps(record))
            translator.mkdir(); (translator/'Translator.Cli.dll').write_bytes(b'published translator')
            (translator/'source-revision').write_text('b'*40+'\n')
            output = base/'tools.tar.xz'
            descriptor = package.build(runtime, translator, output, 'https://example.invalid/tools.tar.xz', 'b'*40)
            self.assertEqual(descriptor['sha256'], hashlib.sha256(output.read_bytes()).hexdigest())
            self.assertEqual(descriptor['size'], output.stat().st_size)
            with tarfile.open(output) as archive:
                names = archive.getnames()
                self.assertIn('rootfs/opt/dolphin-source/Source/tool.cpp', names)
                self.assertIn('rootfs/opt/dolphin-source/LICENSE', names)
                self.assertIn('rootfs/usr/share/doc/component/copyright', names)
                self.assertIn('rootfs/usr/share/dotnet/shared/Microsoft.NETCore.App/8.0/runtime', names)
                self.assertIn('rootfs/opt/kirby-translator/Translator.Cli.dll', names)
                self.assertFalse(any('/.git' in name or '/dotnet/sdk' in name or '/dotnet/packs' in name
                                     or '/opt/freeze' in name or '/powershell' in name for name in names))
                manifest = json.load(archive.extractfile('toolchain.json'))
                self.assertEqual(manifest['translator_revision'], 'b'*40)
                self.assertEqual(descriptor['unpacked_size'], sum(item.size for item in archive if item.isfile()))
            self.assertTrue((runtime/'rootfs/usr/share/dotnet/sdk/8.0/compiler').exists())
            for path in runtime.rglob('*'):
                os.utime(path, (1700000000, 1700000000))
            second = package.build(runtime, translator, base/'same-tools.tar.xz',
                                   'https://example.invalid/tools.tar.xz', 'b'*40)
            self.assertEqual(second['sha256'], descriptor['sha256'])

    def test_packaged_translator_runs_without_building_or_requiring_sdk(self):
        with tempfile.TemporaryDirectory() as temporary:
            assembly = Path(temporary)/'Translator.Cli.dll'; assembly.write_bytes(b'translator')
            with patch.dict('os.environ', {'KIRBY_TRANSLATOR_DLL': str(assembly)}), \
                    patch.object(kirby, 'run_logged', side_effect=AssertionError('Must not build translator')):
                self.assertEqual(kirby.translator(), ['dotnet', assembly])

    def test_missing_packaged_translator_does_not_fall_back_to_sdk(self):
        with patch.dict('os.environ', {'KIRBY_TRANSLATOR_DLL': '/nonexistent/Translator.Cli.dll'}):
            with self.assertRaisesRegex(ValueError, 'Translator DLL not found'):
                kirby.translator()


if __name__ == '__main__':
    unittest.main()
