"""Read-only settings diagnostics shared by the launcher and native executable."""
import json
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
                               capture_output=True, text=True, encoding="utf-8", timeout=15)
                for key in ("KIRBY_NATIVE_TEST", "KIRBY_LAUNCHER_TEST")]

    def test_game_language_choices_reach_the_guest_on_an_existing_nand(self):
        profile = json.loads(Path("recomplauncher.json").read_text())
        choices = next((s["choices"] for s in profile["settings"] if s["id"] == "game_language"), [])
        self.assertEqual({c["label"] for c in choices}, {"English", "Français", "Español"})
        data = Path("disc/DATA").resolve()
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory)/"settings.toml"

            def boot(value, expected):
                config.write_text("[kirby]\nschema_version = 1\n" + value)
                result = subprocess.run([os.environ["KIRBY_NATIVE_TEST"], "--probe-system-config",
                                         "--config", config, "--game-data", data], cwd=directory,
                                        capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f" language={expected};", result.stdout)
            boot("", 1)
            nand = Path(directory)/"local/user/nand"
            sysconf = nand/"shared2/sys/SYSCONF"
            original = sysconf.read_bytes()
            save = nand/"title/00010000/524b3545/data"/"save.bin"
            save.write_bytes(b"existing game data")
            codes = {"English": 1, "Français": 3, "Español": 4}
            for choice in sorted(choices, key=lambda c: c["label"] == "English"):
                value = choice["set"]["kirby/game_language"]
                boot(f"game_language = {value}\n", codes[choice["label"]])
                # An old config with no language preference respects the existing NAND.
                boot("", codes[choice["label"]])
                current = sysconf.read_bytes()
                self.assertEqual(len(current), len(original))
                differences = sum(a != b for a, b in zip(current, original))
                self.assertEqual(differences, int(choice["label"] != "English"))
                self.assertEqual(save.read_bytes(), b"existing game data")

    def test_axis_activation_settings_are_shared_and_invalid_values_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'settings.toml'
            header = '[kirby]\nschema_version = 1\n[controller]\n'
            path.write_text(header+'stick_activation = 0.25\ntrigger_activation = 0.75\n')
            for result in self.invoke_both(path):
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                values = tomllib.loads(result.stdout)['controller']
                self.assertEqual(values['stick_activation'], 0.25)
                self.assertEqual(values['trigger_activation'], 0.75)
            for key in ('stick_activation', 'trigger_activation'):
                for value in ('0.0', '1.0', '-0.1', 'nan', 'inf', 'true', '"0.5"'):
                    with self.subTest(key=key, value=value):
                        text = header+f'{key} = {value}\n';path.write_text(text)
                        for result in self.invoke_both(path):
                            self.assertEqual(result.returncode, 2, result.stdout+result.stderr)
                            self.assertIn(key, result.stderr)
                        self.assertEqual(path.read_text(), text)

    def test_axis_activation_changes_actual_guest_reports_for_virtual_controllers(self):
        data = Path('disc/DATA').resolve()
        for stick, trigger, direction in ((0.25, 0.75, 'right_stick_right'), (0.75, 0.25, 'right_stick_left')):
            with self.subTest(stick=stick, trigger=trigger), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)/'settings.toml'
                path.write_text('[kirby]\nschema_version = 1\n[controller]\n'
                                f'a = "{direction}"\nb = "right_trigger"\n'
                                'up = "left_stick_up"\nstart = "left_trigger"\n'
                                f'stick_activation = {stick}\ntrigger_activation = {trigger}\n')
                result = subprocess.run([os.environ['KIRBY_NATIVE_TEST'], '--probe-configured-controls',
                                         '--config', str(path), '--game-data', str(data), '--volume', '0'],
                                        cwd=directory, capture_output=True, text=True, timeout=30,
                                        env=dict(os.environ, SDL_AUDIO_DRIVER='dummy'))
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                samples = {}
                for line in result.stdout.splitlines():
                    if not line.startswith('[kirby:activation] '):continue
                    profile, token, level, mask = line.removeprefix('[kirby:activation] ').split('|')
                    samples[profile, token, int(level)] = int(mask)
                self.assertEqual(len(samples), 64, result.stdout)
                self.assertEqual(len({key[0] for key in samples}), 4)
                for (profile, token, level), mask in samples.items():
                    threshold = trigger if token.endswith('trigger') else stick
                    pressed = {'right_stick_right': 0x0100, 'right_stick_left': 0x0100,
                               'left_stick_up': 0x0002, 'right_trigger': 0x0200, 'left_trigger': 0x0010}[token]
                    self.assertEqual(mask, pressed if level/100 > threshold else 0,
                                     (profile, token, level, stick, trigger))

    def test_both_programs_read_the_same_settings_without_booting(self):
        with tempfile.TemporaryDirectory(prefix="kirby's settings ") as directory:
            path = Path(directory) / "settings.toml"
            path.write_text('''[kirby]
schema_version = 1
gpu_resident_copies = false
[paths]
dvd_root = "DATA with spaces"
[audio]
volume = 0.375
[video]
vsync = true
resolution_scale = 8
msaa_samples = 4
fxaa = true
''')
            before = path.read_bytes()
            for result in self.invoke_both(path):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                settings = tomllib.loads(result.stdout)
                self.assertEqual(settings["paths"]["dvd_root"], str(Path(directory) / "DATA with spaces"))
                self.assertEqual(settings["audio"]["volume"], 0.375)
                self.assertTrue(settings["video"]["vsync"])
                self.assertEqual(settings["video"]["resolution_scale"], 8)
                self.assertEqual(settings["video"]["msaa_samples"], 4)
                self.assertTrue(settings["video"]["fxaa"])
                self.assertFalse(settings["kirby"]["gpu_resident_copies"])
                self.assertNotIn("translated entry resolved", result.stdout)
            self.assertEqual(path.read_bytes(), before)

    def test_unicode_config_and_data_arguments_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix="kirby 設定 ") as directory:
            path = Path(directory) / "設定 'quoted' file.toml"
            path.write_text('[kirby]\nschema_version = 1\n[paths]\ndvd_root = "資料 DATA"\n', encoding="utf-8")
            for result in self.invoke_both(path):
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                settings = tomllib.loads(result.stdout)
                self.assertEqual(Path(settings["paths"]["dvd_root"]), Path(directory) / "資料 DATA")
            data = Path(directory) / "別の DATA"
            result = subprocess.run([os.environ["KIRBY_NATIVE_TEST"], "--print-settings", str(path),
                                     "--game-data", str(data)], capture_output=True,
                                    text=True, encoding="utf-8", timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(Path(tomllib.loads(result.stdout)["paths"]["dvd_root"]), data)

    def test_invalid_or_future_documents_are_reported_and_preserved(self):
        cases = ('[kirby]\nschema_version = 1\ngame_language = 2\n',
                 '[kirby]\nschema_version = 1\ngame_language = "4"\n',
                 '[broken', '[kirby]\nschema_version = 2\n',
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
                self.assertFalse(settings["video"]["vsync"])
                self.assertTrue(settings["kirby"]["gpu_resident_copies"])
                self.assertEqual(settings['controller']['stick_activation'], 0.5)
                self.assertEqual(settings['controller']['trigger_activation'], 0.5)
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
