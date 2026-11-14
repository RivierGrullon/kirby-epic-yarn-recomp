"""Read-only settings diagnostics shared by the launcher and native executable."""
import os
from pathlib import Path
import subprocess
import tempfile
import tomllib
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_NATIVE_TEST") and os.environ.get("KIRBY_LAUNCHER_TEST"),
                     "Set KIRBY_NATIVE_TEST and KIRBY_LAUNCHER_TEST")
class SharedSettingsTests(unittest.TestCase):
    def invoke_both(self, path):
        return [subprocess.run([os.environ[key], "--print-settings", str(path)],
                               capture_output=True, text=True, timeout=15)
                for key in ("KIRBY_NATIVE_TEST", "KIRBY_LAUNCHER_TEST")]

    def test_both_programs_read_the_same_settings_without_booting(self):
        with tempfile.TemporaryDirectory(prefix="kirby's settings ") as directory:
            path = Path(directory) / "settings.toml"
            path.write_text('''[kirby]
schema_version = 1
gpu_resident_copies = true
[paths]
dvd_root = "DATA with spaces"
[audio]
volume = 0.375
[video]
vsync = false
''')
            before = path.read_bytes()
            for result in self.invoke_both(path):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                settings = tomllib.loads(result.stdout)
                self.assertEqual(settings["paths"]["dvd_root"], str(Path(directory) / "DATA with spaces"))
                self.assertEqual(settings["audio"]["volume"], 0.375)
                self.assertFalse(settings["video"]["vsync"])
                self.assertTrue(settings["kirby"]["gpu_resident_copies"])
                self.assertNotIn("translated entry resolved", result.stdout)
            self.assertEqual(path.read_bytes(), before)

    def test_invalid_or_future_documents_are_reported_and_preserved(self):
        cases = ('[broken', '[kirby]\nschema_version = 2\n',
                 '[kirby]\nschema_version = 1\n[audio]\nvolume = 3\n',
                 '[kirby]\nschema_version = 1\n[video]\nvsync = "false"\n')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.toml"
            for text in cases:
                with self.subTest(text=text):
                    path.write_text(text)
                    for result in self.invoke_both(path):
                        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                        self.assertIn("Settings", result.stderr)
                    self.assertEqual(path.read_text(), text)

    def test_missing_file_uses_defaults_without_creating_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing/settings.toml"
            for result in self.invoke_both(path):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                settings = tomllib.loads(result.stdout)
                self.assertEqual(settings["audio"]["volume"], 1.0)
                self.assertTrue(settings["video"]["vsync"])
                self.assertFalse(settings["kirby"]["gpu_resident_copies"])
            self.assertFalse(path.parent.exists())

    def test_native_cli_values_override_saved_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.toml"
            path.write_text('''[kirby]
schema_version = 1
gpu_resident_copies = true
[audio]
volume = 0.75
[video]
vsync = false
''')
            data = Path(directory) / "explicit DATA"
            result = subprocess.run([os.environ["KIRBY_NATIVE_TEST"], "--print-settings", str(path),
                                     "--volume", "20", "--vsync", "--ram-copies", "--game-data", str(data)],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            settings = tomllib.loads(result.stdout)
            self.assertAlmostEqual(settings["audio"]["volume"], 0.2, places=6)
            self.assertTrue(settings["video"]["vsync"])
            self.assertFalse(settings["kirby"]["gpu_resident_copies"])
            self.assertEqual(settings["paths"]["dvd_root"], str(data))

    def test_controller_and_keyboard_bindings_are_shared(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.toml"
            path.write_text('''[kirby]
schema_version = 1
[controller]
a = "right_shoulder"
start = "back"
back = "start"
[keyboard]
a = "J"
''')
            for result in self.invoke_both(path):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                settings = tomllib.loads(result.stdout)
                self.assertIn("controller", settings, "Gamepad bindings were omitted")
                self.assertEqual(settings["controller"]["a"], "right_shoulder")
                self.assertEqual(settings["controller"]["start"], "back")
                self.assertEqual(settings["controller"]["back"], "start")
                self.assertEqual(settings["keyboard"]["a"], "J")
                self.assertEqual(settings["keyboard"]["start"], "Escape")

    def test_bad_or_conflicting_bindings_are_preserved_and_reported(self):
        cases = ('[controller]\na = "not_a_button"\n',
                 '[controller]\na = "east"\n',
                 '[keyboard]\na = "Escape"\n',
                 '[keyboard]\na = 42\n',
                 '[controller]\na = "disabled"\n[keyboard]\na = "disabled"\n')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.toml"
            for text in cases:
                with self.subTest(text=text):
                    text = '[kirby]\nschema_version = 1\n' + text
                    path.write_text(text)
                    for result in self.invoke_both(path):
                        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                        self.assertIn("Settings", result.stderr)
                    self.assertEqual(path.read_text(), text)

    def test_saved_bindings_reach_original_guest_controller_reports(self):
        data = Path("disc/DATA").resolve()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.toml"
            path.write_text('''[kirby]
schema_version = 1
[controller]
a = "right_shoulder"
start = "back"
back = "start"
[keyboard]
a = "J"
''')
            result = subprocess.run([os.environ["KIRBY_NATIVE_TEST"], "--probe-configured-controls",
                                     "--config", str(path), "--game-data", str(data)],
                                    cwd=directory, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            samples = {}
            for line in result.stdout.splitlines():
                if not line.startswith("[kirby:binding] "): continue
                profile, device, name, value = line.removeprefix("[kirby:binding] ").split("|")
                samples.setdefault(profile, {})[(device, name)] = int(value)
            self.assertEqual(len(samples), 4, result.stdout)
            for profile, values in samples.items():
                with self.subTest(profile=profile):
                    for name, buttons in {"south": 0, "right_shoulder": 0x100, "east": 0x200,
                                          "start": 0x1000, "back": 0x10, "north": 0x800,
                                          "west": 0x400, "dpad_left": 8, "dpad_right": 4,
                                          "dpad_up": 2, "dpad_down": 1, "guide": 0x8000}.items():
                        self.assertEqual(values[("gamepad", name)], buttons)
                    for name, buttons in {"J": 0x100, "Space": 0, "Escape": 0x10,
                                          "Tab": 0x1000, "Left Ctrl": 0x200, "Left": 8,
                                          "Right": 4, "Up": 2, "Down": 1, "X": 0x800, "Z": 0x400, "F1": 0x8000}.items():
                        self.assertEqual(values[("keyboard", name)], buttons)

    def test_signed_stick_and_trigger_bindings_reach_guest_reports(self):
        data = Path("disc/DATA").resolve()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.toml"
            path.write_text('''[kirby]
schema_version = 1
[controller]
a = "left_stick_left"
b = "right_trigger"
up = "right_stick_up"
''')
            result = subprocess.run([os.environ["KIRBY_NATIVE_TEST"], "--probe-configured-controls",
                                     "--config", str(path), "--game-data", str(data)],
                                    cwd=directory, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            samples = {}
            for line in result.stdout.splitlines():
                if not line.startswith("[kirby:binding] "): continue
                profile, device, name, value = line.removeprefix("[kirby:binding] ").split("|")
                samples.setdefault(profile, {})[(device, name)] = int(value)
            self.assertEqual(len(samples), 4, result.stdout)
            for profile, values in samples.items():
                with self.subTest(profile=profile):
                    for name, buttons in {"left_stick_left": 0x100, "left_stick_right": 0,
                                          "right_stick_up": 2, "right_stick_down": 0,
                                          "left_trigger": 0, "right_trigger": 0x200}.items():
                        self.assertEqual(values[("axis", name)], buttons)
                    for name in ("south", "east", "dpad_up"):
                        self.assertEqual(values[("gamepad", name)], 0)
