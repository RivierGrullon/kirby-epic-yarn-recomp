"""Opt-in native Vulkan checks; require game data and a host graphics session."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_NATIVE_VIDEO_TEST"),
                     "Set KIRBY_NATIVE_VIDEO_TEST to test the native Vulkan executable")
class NativeVideoTests(unittest.TestCase):
    def check_startup_and_exit(self, *arguments):
        executable = Path(os.environ["KIRBY_NATIVE_VIDEO_TEST"]).resolve()
        data = Path("disc/DATA").resolve()
        with tempfile.TemporaryDirectory(prefix="kirby-native-video-") as directory:
            log_path = Path(directory) / "native.log"
            with log_path.open("wb") as log:
                process = subprocess.Popen(
                    [str(executable), "--video-hidden", "--volume", "0", "--game-data", str(data), *arguments],
                    cwd=directory, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    milestones = ("WPAD initialized", "verified AXWii task loaded", "first GX draw submitted")
                    while time.monotonic() < deadline:
                        output = log_path.read_text(errors="replace")
                        if all(milestone in output for milestone in milestones):
                            break
                        self.assertIsNone(process.poll(), output)
                        time.sleep(0.05)
                    else:
                        self.fail("Native startup deadline expired:\n" + output)
                    process.terminate()
                    try:
                        result = process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        self.fail("Native runtime ignored SIGTERM:\n" + log_path.read_text(errors="replace"))
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)
            output = log_path.read_text(errors="replace")
            self.assertEqual(result, 0, output)
            self.assertIn("host exit requested; shutting down native runtime", output)
            self.assertNotIn("[kirby] blocked:", output)

    def test_sigterm_exits_after_native_video_controller_and_ax_task_startup(self):
        self.check_startup_and_exit()

    def test_native_video_without_vsync_starts_and_exits_normally(self):
        self.check_startup_and_exit("--no-vsync")
