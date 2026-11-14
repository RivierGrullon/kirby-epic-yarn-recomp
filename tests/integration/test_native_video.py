"""Opt-in native Vulkan checks; require game data and a host graphics session."""
import os
import csv
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_NATIVE_VIDEO_TEST"),
                     "Set KIRBY_NATIVE_VIDEO_TEST to test the native Vulkan executable")
class NativeVideoTests(unittest.TestCase):
    def check_startup_and_exit(self, *arguments, frame_log=False, settings_file=False):
        executable = Path(os.environ["KIRBY_NATIVE_VIDEO_TEST"]).resolve()
        data = Path("disc/DATA").resolve()
        with tempfile.TemporaryDirectory(prefix="kirby-native-video-") as directory:
            log_path = Path(directory) / "native.log"
            metrics_path = Path(directory) / "frames.csv"
            launch_options = ["--volume", "0", "--game-data", str(data)]
            if settings_file:
                config_path = Path(directory) / "settings.toml"
                config_path.write_text('[kirby]\nschema_version = 1\ngpu_resident_copies = true\n'
                                       '[paths]\ndvd_root = ' + json.dumps(str(data)) + '\n'
                                       '[audio]\nvolume = 0.0\n[video]\nvsync = false\n')
                launch_options = ["--config", str(config_path)]
            if frame_log:
                arguments=(*arguments,"--frame-log",str(metrics_path))
            with log_path.open("wb") as log:
                process = subprocess.Popen(
                    [str(executable), "--video-hidden", *launch_options, *arguments],
                    cwd=directory, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    milestones = ("WPAD initialized", "verified AXWii task loaded", "first GX draw submitted")
                    while time.monotonic() < deadline:
                        output = log_path.read_text(errors="replace")
                        # The first CSV row is flushed explicitly. stdout may remain
                        # buffered after the first presentation, so it is not a clock.
                        recorded_frame = not frame_log or (
                            metrics_path.exists() and
                            len(metrics_path.read_text().splitlines()) > 1)
                        if all(milestone in output for milestone in milestones) and recorded_frame:
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
            if settings_file or "--gpu-resident-copies" in arguments:
                self.assertIn("experimental GPU-resident texture and display copies enabled", output)
            if frame_log:
                with metrics_path.open() as metrics:
                    rows=[{key:int(value) for key,value in row.items()} for row in csv.DictReader(metrics)]
                self.assertGreater(len(rows),0)
                self.assertEqual([row["sequence"] for row in rows],list(range(1,len(rows)+1)))
                previous=0
                for row in rows:
                    stamps=[row[key] for key in ("start_ns","seal_start_ns","seal_end_ns","wait_end_ns","finish_ns")]
                    self.assertEqual(stamps,sorted(stamps))
                    self.assertGreaterEqual(stamps[0],previous)
                    self.assertGreater(stamps[-1],stamps[0])
                    self.assertGreater(row["draws"],0)
                    previous=stamps[-1]

    def test_sigterm_exits_after_native_video_controller_and_ax_task_startup(self):
        self.check_startup_and_exit()

    def test_native_video_without_vsync_starts_and_exits_normally(self):
        self.check_startup_and_exit("--no-vsync")

    def test_experimental_performance_mode_starts_and_exits_normally(self):
        self.check_startup_and_exit("--gpu-resident-copies", "--no-vsync")

    def test_frame_log_records_actual_native_presentations(self):
        self.check_startup_and_exit("--gpu-resident-copies","--no-vsync",frame_log=True)

    def test_saved_configuration_drives_native_video_startup(self):
        self.check_startup_and_exit(settings_file=True)
