# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import build_appimage


class AppImageBuilderTests(unittest.TestCase):
    def test_cached_runtime_is_checked_before_becoming_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);cache=root/'local/packaging';cache.mkdir(parents=True)
            expected=hashlib.sha256(b'fixture runtime').hexdigest()
            runtime=cache/('runtime-'+expected[:12]+'-x86_64')
            runtime.write_bytes(b'fixture runtime');runtime.chmod(0o600)
            with patch.object(build_appimage,'ROOT',root),patch.object(build_appimage,'RUNTIME_SHA256',expected):
                self.assertEqual(build_appimage.runtime_file(),runtime)
            self.assertTrue(os.access(runtime,os.X_OK))

    def test_corrupt_cached_runtime_is_rejected_and_not_executed(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);cache=root/'local/packaging';cache.mkdir(parents=True)
            expected=hashlib.sha256(b'fixture runtime').hexdigest()
            runtime=cache/('runtime-'+expected[:12]+'-x86_64')
            runtime.write_bytes(b'damaged');runtime.chmod(0o600)
            with patch.object(build_appimage,'ROOT',root),patch.object(build_appimage,'RUNTIME_SHA256',expected):
                with self.assertRaisesRegex(ValueError,'checksum'):
                    build_appimage.runtime_file()
            self.assertFalse(os.access(runtime,os.X_OK))

    def test_compact_image_refuses_an_embedded_build_environment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);appdir=root/'image'
            for name in ('AppRun','revision','project/tools/kirby.py','tools/runtime/kirby-setup-worker',
                         'tools/runtime/ca-certificates.crt'):
                path=appdir/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            record=dict(version=1,platform='linux-x86_64',image_id='a'*64,sha256='b'*64,
                        url='https://example.invalid/tools.tar.xz',size=100,unpacked_size=200)
            (appdir/'tools/runtime/toolchain-download.json').write_text(json.dumps(record))
            (appdir/'tools/runtime/rootfs').mkdir()
            with self.assertRaisesRegex(ValueError,'separate download'):
                build_appimage.build(appdir,root/'game.AppImage')
            self.assertFalse((root/'game.AppImage').exists())


if __name__=='__main__':
    unittest.main()
