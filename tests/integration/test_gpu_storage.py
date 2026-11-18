# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in Vulkan regression for large indexed arrays; no game data required."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('KIRBY_GPU_STORAGE_TEST'),
                     'Set KIRBY_GPU_STORAGE_TEST to the native renderer fixture')
class GpuStorageTests(unittest.TestCase):
    def test_large_indexed_arrays_render_independent_snapshots_in_one_frame(self):
        executable = Path(os.environ['KIRBY_GPU_STORAGE_TEST']).resolve()
        with tempfile.TemporaryDirectory(prefix='kirby-gpu-storage-') as directory:
            result = subprocess.run([str(executable), '--gpu-storage-hidden'],
                cwd=directory, env=dict(os.environ, SDL_AUDIO_DRIVER='dummy'),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout[-6000:])
