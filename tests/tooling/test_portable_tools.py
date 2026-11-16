# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from installer import portable


class PortableToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kirby portable $ literal ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = self.root/'tools/runtime'
        self.base.mkdir(parents=True)
        self.manifest = dict(version=1, platform='linux-x86_64', image_id='a'*64, files={})

    def bundle(self):
        for name in ('proot', 'rootfs/usr/bin/python3.12', 'rootfs/usr/bin/git',
                     'rootfs/usr/bin/clang', 'rootfs/usr/bin/cmake', 'rootfs/usr/bin/ninja',
                     'rootfs/usr/bin/7z', 'rootfs/usr/bin/dolphin-tool', 'rootfs/usr/bin/dotnet'):
            path = self.base/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture executable')
            path.chmod(0o700)
            self.manifest['files'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.base/'lib').mkdir()
        (self.base/'toolchain.json').write_text(json.dumps(self.manifest))
        return portable.PortableTools.find(self.root)

    def test_developer_checkout_does_not_require_a_bundle(self):
        self.assertIsNone(portable.PortableTools.find(self.root))

    def test_damaged_bundle_is_rejected_without_falling_back_to_host_tools(self):
        self.bundle()
        (self.base/'proot').write_bytes(b'damaged')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            portable.PortableTools.find(self.root)

    def test_manifest_cannot_select_files_outside_the_bundle(self):
        self.bundle()
        self.manifest['files']['../outside'] = 'a'*64
        (self.base/'toolchain.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'path'):
            portable.PortableTools.find(self.root)

    def test_command_keeps_literal_arguments_and_binds_external_source(self):
        tools = self.bundle()
        source = self.root.parent/"own game ' $(literal).7z"
        arguments = ['7z', 'l', '-slt', '--', str(source)]
        command = tools.command(arguments, binds=[source.parent])
        self.assertEqual(command[-len(arguments):], arguments)
        self.assertIn(str(self.root)+':/workspace', command)
        self.assertIn(str(source.parent)+':'+str(source.parent), command)
        self.assertEqual(command[0], str(self.base/'proot'))
        self.assertEqual(tools.image_id, 'a'*64)

    def test_build_environment_does_not_inherit_host_python_or_graphics_libraries(self):
        tools = self.bundle()
        env = tools.environment({'PATH':'/host-only', 'PYTHONHOME':'/bad-python',
                                 'LD_LIBRARY_PATH':'/bad-libraries', 'DISPLAY':':0'})
        self.assertNotIn('/host-only', env['PATH'])
        self.assertNotIn('PYTHONHOME', env)
        self.assertNotIn('DISPLAY', env)
        self.assertNotIn('/bad-libraries', env['LD_LIBRARY_PATH'])
        self.assertEqual(env['GIT_CONFIG_VALUE_0'], '/workspace')


if __name__ == '__main__':
    unittest.main()
