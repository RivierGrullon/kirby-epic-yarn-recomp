# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import build_native


class SnapshotPatchTests(unittest.TestCase):
    def test_crlf_snapshot_and_patch_apply_without_changing_upstream(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            subprocess.run(['git','init',str(root)],check=True,capture_output=True)
            upstream=root/'vendor/source.cpp';upstream.parent.mkdir()
            upstream.write_bytes(b'int value = 1;\r\n')
            workspace=root/'local/snapshot';target=workspace/'runtime/source.cpp'
            target.parent.mkdir(parents=True)
            shutil.copyfile(upstream,target)
            diff=root/'change.patch'
            diff.write_bytes(b'--- a/runtime/source.cpp\r\n+++ b/runtime/source.cpp\r\n@@ -1 +1 @@\r\n-int value = 1;\r\n+int value = 2;\r\n')
            with patch.object(build_native,'ROOT',root):
                build_native.apply_snapshot_patch(diff,workspace)
                build_native.apply_snapshot_patch(diff,workspace)
            self.assertEqual(target.read_bytes(),b'int value = 2;\n')
            self.assertEqual(upstream.read_bytes(),b'int value = 1;\r\n')

    def test_snapshot_semantic_edits_are_preserved_on_patch_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            subprocess.run(['git','init',str(root)],check=True,capture_output=True)
            workspace=root/'snapshot';target=workspace/'runtime/source.cpp'
            target.parent.mkdir(parents=True);target.write_bytes(b'int value = 42;\n')
            diff=root/'change.patch';diff.write_text('--- a/runtime/source.cpp\n+++ b/runtime/source.cpp\n@@ -1 +1 @@\n-int value = 1;\n+int value = 2;\n')
            with patch.object(build_native,'ROOT',root),self.assertRaisesRegex(ValueError,'does not apply'):
                build_native.apply_snapshot_patch(diff,workspace)
            self.assertEqual(target.read_bytes(),b'int value = 42;\n')
