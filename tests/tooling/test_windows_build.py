# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from windows import windows_build


class WindowsBuildTests(unittest.TestCase):
    def run_build(self, source, root):
        with patch.object(windows_build, 'ROOT', root), patch.object(windows_build, 'package'):
            windows_build.build(source, jobs=4, translated_jobs=2,
                                mingw=root / 'compiler', cppwinrt=root / 'headers', redist=root / 'redist.exe')

    def test_invalid_dol_stops_before_bootstrap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'main.dol'
            source.write_bytes(b'invalid')
            with patch.object(windows_build.subprocess, 'run') as run:
                with self.assertRaises(ValueError):
                    self.run_build(source, root)
                run.assert_not_called()

    def test_wbfs_passes_verified_export_to_translation(self):
        with tempfile.TemporaryDirectory(prefix="build spaces '") as directory:
            root = Path(directory)
            source = root / "game's copy.WBFS"
            source.touch()
            data = root / 'export/DATA'
            (data / 'sys').mkdir(parents=True)
            (data / 'sys/main.dol').write_bytes(b'verified fixture')
            with patch.object(windows_build, 'inspect_source', return_value={'data_root': str(data)}) as inspect, \
                 patch.object(windows_build, 'verify_hash'), \
                 patch.object(windows_build.subprocess, 'run') as run:
                self.run_build(source, root)
            self.assertEqual(inspect.call_args.args[0], source.resolve())
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(len(commands), 3)
            self.assertEqual(Path(commands[0][1]).name, 'bootstrap.py')
            self.assertEqual(commands[1][2:6], ['translate', '--dol', str(data / 'sys/main.dol'), '--threads'])
            self.assertIn('--cppwinrt-include', commands[2])
            self.assertEqual(commands[2][-1], str(root / 'headers'))

    def test_failed_translation_never_starts_native_build_and_releases_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'main.dol'
            source.write_bytes(b'fixture')
            with patch.object(windows_build, 'verify_hash'), \
                 patch.object(windows_build.subprocess, 'run', side_effect=[None, subprocess.CalledProcessError(7, 'translate')]) as run:
                with self.assertRaises(subprocess.CalledProcessError):
                    self.run_build(source, root)
                self.assertEqual(run.call_count, 2)
            # A later invocation can acquire the same setup workspace.
            with patch.object(windows_build, 'verify_hash'), patch.object(windows_build.subprocess, 'run'):
                self.run_build(source, root)
