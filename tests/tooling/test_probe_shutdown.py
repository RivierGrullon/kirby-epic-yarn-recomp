# SPDX-License-Identifier: GPL-3.0-only
import os
from pathlib import Path
import signal
import sys
import tempfile
import textwrap
import time
import unittest
from unittest.mock import patch

if os.name == 'posix':
    import fcntl

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from installer.jobs import Cancelled, Job, readable, run_process


# Keep stdout open after SIGTERM. The lock lets the test observe when this
# descendant stops, including when its exited process remains a zombie.
DESCENDANT = textwrap.dedent('''\
    from pathlib import Path
    import fcntl
    import signal
    import time

    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    with Path('descendant.lock').open('wb') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        Path('descendant.ready').touch()
        time.sleep(12)
    ''')


@unittest.skipUnless(os.name == 'posix', 'requires POSIX process groups and signals')
class ProbeShutdownTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.job = self.enterContext(Job(self.directory / 'job', 'probe', lambda _: None))
        self.output = bytearray()
        self.delayed_pipe = False

    def run_probe(self, script, *, timeout=4, stop_timeout=3, on_output=None, capture_limit=4096):
        def observe(data):
            self.output.extend(data)
            if on_output is not None:
                on_output(self.output)

        return run_process(self.job, [sys.executable, '-u', '-c', textwrap.dedent(script)],
                           cwd=self.directory, capture_limit=capture_limit, on_output=observe,
                           stop_when=lambda: b'ready\n' in self.output,
                           timeout=timeout, stop_timeout=stop_timeout)

    def delay_ready_pipe(self, *, delay, when):
        # Simulate a scheduler stall after the real poll observes pipe readiness.
        # The pipe, child exit status, and monotonic clock all remain real.
        def poll(*args, **kwargs):
            pipe_ready = readable(*args, **kwargs)
            if pipe_ready and when():
                self.delayed_pipe = True
                time.sleep(delay)
            return pipe_ready

        return patch('installer.jobs.readable', new=poll)

    def test_startup_deadline_applies_after_waiting_for_eof(self):
        with self.delay_ready_pipe(delay=1.15, when=lambda: True):
            with self.assertRaises(TimeoutError):
                run_process(self.job, [sys.executable, '-c', 'pass'], timeout=1)
        self.assertTrue(self.delayed_pipe)

    def test_shutdown_deadline_applies_after_waiting_for_eof(self):
        with self.delay_ready_pipe(delay=0.6, when=lambda: b'ready\n' in self.output):
            with self.assertRaises(TimeoutError):
                self.run_probe('''
                    import signal
                    import time

                    def stop(*_):
                        raise SystemExit(0)

                    signal.signal(signal.SIGTERM, stop)
                    print('ready')
                    time.sleep(12)
                    raise SystemExit(24)
                    ''', stop_timeout=0.35)
        self.assertTrue(self.delayed_pipe)

    def test_invalid_shutdown_deadline_is_rejected_before_launch(self):
        for stop_timeout in (0, -1, float('inf'), float('nan'), None):
            with self.subTest(stop_timeout=stop_timeout):
                with self.assertRaises(ValueError):
                    self.run_probe('''
                        from pathlib import Path

                        Path('spawned').touch()
                        ''', stop_timeout=stop_timeout)
                self.assertFalse((self.directory / 'spawned').exists())

    def test_ready_probe_can_exit_after_startup_deadline(self):
        output = self.run_probe('''
            import signal
            import time

            def stop(*_):
                print('stopping')
                time.sleep(1.5)
                print('finished')
                raise SystemExit(0)

            signal.signal(signal.SIGTERM, stop)
            time.sleep(0.35)
            print('ready')
            time.sleep(12)
            raise SystemExit(24)
            ''', timeout=1.25, stop_timeout=4)
        self.assertEqual(output, b'ready\nstopping\nfinished\n')

    def test_timeout_kills_stuck_probe_and_descendant(self):
        self.check_group_timeout(leader_exits=False)

    def test_descendant_holding_stdout_after_leader_exit_is_bounded(self):
        self.check_group_timeout(leader_exits=True)

    def test_probe_closing_stdout_before_exit_is_bounded(self):
        with self.assertRaises(TimeoutError):
            self.run_probe('''
                import os
                import signal
                import time

                def stop(*_):
                    print('stopping')
                    os.close(1)
                    os.close(2)

                signal.signal(signal.SIGTERM, stop)
                print('ready')
                time.sleep(12)
                ''', timeout=8, stop_timeout=1)
        self.assertIn(b'ready\nstopping\n', self.output)

    def check_group_timeout(self, *, leader_exits):
        self.addCleanup(self.stop_lingering_fixture)
        started = time.monotonic()
        with self.assertRaises(TimeoutError):
            self.run_probe(f'''
                from pathlib import Path
                import os
                import signal
                import subprocess
                import sys
                import time

                def stop(*_):
                    with Path('shutdown.requests').open('a') as requests:
                        requests.write('SIGTERM\\n')
                    print('stopping')
                    if {leader_exits!r}:
                        raise SystemExit(0)

                signal.signal(signal.SIGTERM, stop)
                Path('leader.pid').write_text(str(os.getpid()))
                subprocess.Popen([sys.executable, '-c', {DESCENDANT!r}])
                while not Path('descendant.ready').exists():
                    time.sleep(0.01)
                print('ready')
                time.sleep(12)
                raise SystemExit(24)
                ''', timeout=8, stop_timeout=1)
        self.assertIn(b'ready\nstopping\n', self.output)
        self.assertEqual((self.directory / 'shutdown.requests').read_text().splitlines(), ['SIGTERM'])
        self.assertLess(time.monotonic() - started, 6)
        leader = int((self.directory / 'leader.pid').read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(leader, 0)
        deadline = time.monotonic() + 2
        while self.descendant_holds_lock() and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertFalse(self.descendant_holds_lock(), 'probe descendant still holds its lock')

    def descendant_holds_lock(self):
        try:
            with (self.directory / 'descendant.lock').open('rb') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return True
        except FileNotFoundError:
            pass
        return False

    def stop_lingering_fixture(self):
        if self.descendant_holds_lock():
            leader = int((self.directory / 'leader.pid').read_text())
            try:
                os.killpg(leader, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def test_nonzero_exit_during_graceful_stop_preserves_status(self):
        with self.assertRaisesRegex(ValueError, r'\b23\b'):
            self.run_probe('''
                import signal
                import time

                def stop(*_):
                    print('stopping')
                    raise SystemExit(23)

                signal.signal(signal.SIGTERM, stop)
                print('ready')
                time.sleep(12)
                ''')
        self.assertIn(b'ready\nstopping\n', self.output)

    def test_cancellation_during_graceful_stop_is_reported(self):
        def cancel_after_stop(output):
            if b'stopping\n' in output:
                self.job.cancel.set()

        with self.assertRaises(Cancelled):
            self.run_probe('''
                import signal
                import time

                def stop(*_):
                    print('stopping')
                    time.sleep(0.5)
                    raise SystemExit(0)

                signal.signal(signal.SIGTERM, stop)
                print('ready')
                time.sleep(12)
                ''', on_output=cancel_after_stop)
        self.assertIn(b'ready\nstopping\n', self.output)

    def test_shutdown_output_counts_toward_capture_limit(self):
        with self.assertRaises(ValueError):
            self.run_probe('''
                import signal
                import time

                def stop(*_):
                    print('stopping' * 32)
                    raise SystemExit(0)

                signal.signal(signal.SIGTERM, stop)
                print('ready')
                time.sleep(12)
                ''', capture_limit=64)
        self.assertIn(b'stopping' * 32, self.output)

    def test_late_readiness_exceeds_startup_deadline(self):
        with self.assertRaises(TimeoutError):
            self.run_probe('''
                import signal
                import time

                signal.signal(signal.SIGTERM, signal.SIG_IGN)
                time.sleep(1)
                print('ready')
                time.sleep(0.25)
                ''', timeout=0.4, stop_timeout=3)
        self.assertNotIn(b'ready\n', self.output)
