# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.inputs import build_keys,compile_jobs


class BuildInputsTests(unittest.TestCase):
    def test_jobs_are_bounded_by_memory_and_cpu(self):
        gib=1024**3
        self.assertEqual(compile_jobs(8,12*gib),4)
        self.assertEqual(compile_jobs(2,12*gib),2)
        self.assertEqual(compile_jobs(8,3*gib),1)
        with self.assertRaises(ValueError):compile_jobs(8,gib)

    def test_runtime_changes_keep_translation_cache_but_generator_changes_invalidate_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in ('projects/RK5E01/recomp.yml','tools/kirby.py','tools/audits/example.py',
                         'runtime/native-src/hook.cpp','runtime/include/abi.h','runtime/src/game.cpp',
                         'patches/wiicompiled/test.patch','dependencies.json'):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('initial')
            identity=dict(source_revision='a'*40,source_modified=False,source_sha256='b'*64)
            first=build_keys(root,identity,'c'*64)
            (root/'runtime/src/game.cpp').write_text('runtime changed')
            changed=dict(identity,source_sha256='d'*64)
            second=build_keys(root,changed,'c'*64)
            self.assertEqual(first['translation'],second['translation'])
            self.assertNotEqual(first['native'],second['native'])
            (root/'tools/kirby.py').write_text('generator changed')
            third=build_keys(root,changed,'c'*64)
            self.assertNotEqual(second['translation'],third['translation'])
            self.assertNotEqual(second['cache'],build_keys(root,changed,'e'*64)['cache'])
            (root/'tools/build_layout.py').write_text('changed output layout')
            self.assertNotEqual(third['translation'],build_keys(root,changed,'c'*64)['translation'])
