# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in link-wrapper verification using the real hidden Vulkan renderer."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from dawn_stats import read_events, summarize


@unittest.skipUnless(os.environ.get('KIRBY_DAWN_PROFILE_TEST'),
                     'Set KIRBY_DAWN_PROFILE_TEST to an instrumented renderer fixture')
class DawnProfileTests(unittest.TestCase):
    def run_renderer(self, directory, trace):
        program = Path(os.environ['KIRBY_DAWN_PROFILE_TEST']).resolve()
        environment = dict(os.environ, SDL_AUDIO_DRIVER='dummy', KIRBY_DAWN_RESOURCE_LOG=str(trace))
        result = subprocess.run([program, '--gpu-xfb-hidden'], cwd=directory, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout[-6000:])
        return result.stdout

    def test_actual_calls_and_threads_are_recorded(self):
        with tempfile.TemporaryDirectory(prefix='kirby-dawn-profile-') as directory:
            trace = Path(directory)/'resources.csv'
            self.run_renderer(directory, trace)
            with trace.open(newline='') as stream:
                events = read_events(stream)
            operations = {row['operation'] for row in events}
            self.assertTrue({'CreateTexture', 'CreateView', 'CreateBindGroup', 'QueueSubmit',
                             'PresentSurface', 'FrameWorkerWait'} <= operations)
            report = summarize(events)
            self.assertGreater(report['guest_resource_calls'], 0)
            self.assertTrue(any(row['operation'] == 'QueueSubmit' and row['tid'] != report['guest_tid']
                                for row in events))
            self.assertEqual(trace.stat().st_mode & 0o777, 0o600)

    def test_existing_files_and_symlinks_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix='kirby-dawn-profile-') as directory:
            root = Path(directory)
            existing = root/'existing.csv'
            existing.write_bytes(b'keep original trace\n')
            link = root/'linked.csv'
            link.symlink_to(existing)
            for trace in (existing, link):
                with self.subTest(path=trace):
                    output = self.run_renderer(directory, trace)
                    self.assertIn('Cannot create exclusive trace', output)
                    self.assertEqual(existing.read_bytes(), b'keep original trace\n')
                    self.assertTrue(link.is_symlink())


if __name__ == '__main__':
    unittest.main()
