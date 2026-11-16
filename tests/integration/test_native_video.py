"""Opt-in native Vulkan checks; require game data and a host graphics session."""
import os
import csv
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


def close_native_window(process):
    if os.name != "nt":
        process.terminate()
        return
    # Windows terminate() kills the process; WM_CLOSE exercises SDL's normal
    # shutdown path. Only address windows belonging to this test's child PID.
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    closed = []

    @callback_type
    def visit(window, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process.pid and user32.PostMessageW(window, 0x0010, 0, 0):
            closed.append(window)
        return True

    user32.EnumWindows(visit, 0)
    if not closed:
        raise RuntimeError("Test child has no window accepting WM_CLOSE")


@unittest.skipUnless(os.environ.get("KIRBY_NATIVE_VIDEO_TEST"),
                     "Set KIRBY_NATIVE_VIDEO_TEST to test the native Vulkan executable")
class NativeVideoTests(unittest.TestCase):
    def check_startup_and_exit(self, *arguments, frame_log=False, settings_file=False, quality=None):
        executable = Path(os.environ["KIRBY_NATIVE_VIDEO_TEST"]).resolve()
        data = Path("disc/DATA").resolve()
        with tempfile.TemporaryDirectory(prefix="kirby-native-video-") as directory:
            log_path = Path(directory) / "native.log"
            metrics_path = Path(directory) / "frames.csv"
            launch_options = ["--volume", "0", "--game-data", str(data)]
            if settings_file or quality:
                config_path = Path(directory) / "settings.toml"
                resident = quality[2] if quality else True
                config_path.write_text('[kirby]\nschema_version = 1\ngpu_resident_copies = ' + str(resident).lower() + '\n'
                                       '[paths]\ndvd_root = ' + json.dumps(str(data)) + '\n'
                                       '[audio]\nvolume = 0.0\n[video]\nvsync = false\n' +
                                       (f'resolution_scale = {quality[0]}\nmsaa_samples = {quality[1]}\n' if quality else '') +
                                       (f'fxaa = {str(quality[3]).lower()}\n' if quality and len(quality)>3 else ''))
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
                            len(metrics_path.read_text().splitlines()) > (120 if quality else 1))
                        if all(milestone in output for milestone in milestones) and recorded_frame:
                            break
                        self.assertIsNone(process.poll(), output)
                        time.sleep(0.05)
                    else:
                        self.fail("Native startup deadline expired:\n" + output)
                    close_native_window(process)
                    try:
                        result = process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        self.fail("Native runtime ignored shutdown request:\n" + log_path.read_text(errors="replace"))
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)
            output = log_path.read_text(errors="replace")
            self.assertEqual(result, 0, output)
            self.assertIn("host exit requested; shutting down native runtime", output)
            self.assertNotIn("[kirby] blocked:", output)
            if quality:
                self.assertIn(f"internal resolution={quality[0]}x; MSAA={quality[1]}", output)
                if len(quality)>3:
                    self.assertIn(f"FXAA={'on' if quality[3] else 'off'}", output)
                import re
                copies = re.findall(r"size=(\d+)x(\d+) bytes=\d+ GPU size=(\d+)x(\d+)", output)
                self.assertTrue(copies, output)
                for width, height, gpu_width, gpu_height in copies:
                    self.assertEqual(int(gpu_width), int(width) * quality[0])
                    self.assertEqual(int(gpu_height), int(height) * quality[0])
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

    def test_resolution_and_msaa_with_synchronous_and_resident_copies(self):
        for quality in ((1, 4, False), (3, 4, False), (3, 4, True), (6, 4, True)):
            with self.subTest(scale=quality[0], samples=quality[1], resident=quality[2]):
                self.check_startup_and_exit(frame_log=True, quality=quality)

    def test_extended_image_quality(self):
        for quality in ((8, 1, False, False), (8, 4, True, False),
                        (3, 1, False, True), (8, 4, True, True)):
            with self.subTest(quality=quality):
                self.check_startup_and_exit(frame_log=True, quality=quality)
