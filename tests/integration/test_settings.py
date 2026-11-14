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
