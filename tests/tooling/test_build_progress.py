# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from installer.progress import ToolProgress
from installer.jobs import Job, run_process
import kirby


class BuildProgressTests(unittest.TestCase):
    def test_compile_progress_handles_split_output_and_reports_tasks_not_bytes(self):
        events = []
        progress = ToolProgress('compile', events.append)
        progress(b'Configuring dependencies\n[1/25] Building CXX object\n[2/')
        progress(b'25] Building CXX object\n[25/25] Linking executable\n')
        self.assertEqual([(e['completed'], e['total']) for e in events], [(1,25), (2,25), (25,25)])
        self.assertTrue(all(e['stage'] == 'compile' and e['unit'] == 'tasks' for e in events))

    def test_compile_log_is_streamed_while_the_tool_is_still_running(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            from build_layout import BuildLayout
            layout = BuildLayout(root, root/'local/setup/builds/progress')
            ack = root/'output-was-shown'
            class VisibleOutput(io.StringIO):
                def write(inner, text):
                    if '[1/2] Building' in text:
                        ack.touch()
                    return super().write(text)
            buffer = VisibleOutput()
            command = [sys.executable, '-u', '-c',
                       "import pathlib,time,sys;print('[1/2] Building',flush=True);"
                       f"p=pathlib.Path({str(ack)!r});"
                       "\nfor _ in range(40):\n if p.exists(): break\n time.sleep(.05)\n"
                       "if not p.exists(): sys.exit(9)\nprint('[2/2] Linking')"]
            with contextlib.redirect_stdout(buffer):
                kirby.run_logged('compile', command, layout=layout)
            self.assertIn('[1/2] Building', buffer.getvalue())
            self.assertIn('[2/2] Linking', (layout.logs/'compile.log').read_text())

    def test_noisy_or_untrusted_output_never_creates_invalid_progress(self):
        events = []; progress = ToolProgress('compile', events.append)
        progress(b'x'*100000 + b'\n[4/2] invalid\n[1/0] invalid\n[0/2] starting\n')
        self.assertEqual([(e['completed'], e['total']) for e in events], [(0,2)])

    def test_real_process_output_updates_progress_before_exit(self):
        with tempfile.TemporaryDirectory() as temporary:
            events = []
            with Job(Path(temporary), 'progress', events.append) as job:
                run_process(job, [sys.executable, '-u', '-c', "print('[1/2] Building');print('[2/2] Linking')"],
                            on_output=ToolProgress('compile', job.emit))
            self.assertEqual(events[-1]['completed'], 2)


if __name__ == '__main__':
    unittest.main()
