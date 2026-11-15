# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in real Vulkan readback trace; requires matching Tracy CLI tools."""
import csv
import io
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(all(os.environ.get(name) for name in (
    'KIRBY_GPU_TRACE_TEST', 'KIRBY_TRACY_CAPTURE', 'KIRBY_TRACY_EXPORT')),
    'Set KIRBY_GPU_TRACE_TEST, KIRBY_TRACY_CAPTURE and KIRBY_TRACY_EXPORT')
class GpuTraceTests(unittest.TestCase):
    def test_readback_submissions_keep_reporting_gpu_work(self):
        program = Path(os.environ['KIRBY_GPU_TRACE_TEST']).resolve()
        capture = Path(os.environ['KIRBY_TRACY_CAPTURE']).resolve()
        export = Path(os.environ['KIRBY_TRACY_EXPORT']).resolve()
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        environment = dict(os.environ, SDL_AUDIO_DRIVER='dummy', TRACY_PORT=str(port))
        with tempfile.TemporaryDirectory(prefix='kirby-gpu-trace-') as directory:
            root = Path(directory)
            trace = root/'readbacks.tracy'
            renderer_log = root/'renderer.log'
            capture_log = root/'capture.log'
            processes = []
            with renderer_log.open('wb') as renderer_output, capture_log.open('wb') as capture_output:
                try:
                    renderer = subprocess.Popen([program, '--gpu-trace-hidden'], cwd=root,
                        env=environment, stdout=renderer_output, stderr=subprocess.STDOUT)
                    processes.append(renderer)
                    deadline = time.monotonic() + 15
                    while 'Waiting for the local Tracy collector' not in renderer_log.read_text(errors='replace'):
                        self.assertIsNone(renderer.poll(), renderer_log.read_text(errors='replace'))
                        self.assertLess(time.monotonic(), deadline, 'GPU fixture readiness timed out')
                        time.sleep(.02)
                    collector = subprocess.Popen([capture, '-a', '127.0.0.1', '-p', str(port),
                        '-o', trace, '-s', '8'], cwd=root, env=environment,
                        stdout=capture_output, stderr=subprocess.STDOUT)
                    processes.append(collector)
                    result = renderer.wait(timeout=30)
                    self.assertEqual(result, 0, renderer_log.read_text(errors='replace'))
                    self.assertEqual(collector.wait(timeout=20), 0, capture_log.read_text(errors='replace'))
                finally:
                    for process in reversed(processes):
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=3)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait(timeout=3)
            output = renderer_log.read_text(errors='replace')
            self.assertIn('Vulkan XFB checks passed', output)
            self.assertTrue(trace.is_file(), capture_log.read_text(errors='replace'))
            exported = subprocess.run([export, '-g', trace], cwd=root, env=environment,
                capture_output=True, text=True, timeout=30, check=True)
            rows = list(csv.DictReader(io.StringIO(exported.stdout)))
            frames = [row for row in rows if row['name'] == 'Frame']
            transfers = [row for row in rows if row['name'] == 'Readback transfer']
            self.assertGreaterEqual(len(frames), 3,
                'GPU profiling stopped across readback submissions: '+exported.stdout[:2000])
            self.assertGreaterEqual(len(transfers), 3, 'Readback GPU timings are missing')
            for row in frames + transfers:
                self.assertGreater(int(row['GPU execution time']), 0)
