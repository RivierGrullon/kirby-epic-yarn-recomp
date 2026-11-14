# SPDX-License-Identifier: GPL-3.0-only
"""Read-only identity must work without disc data, SDL devices or settings."""
import os
from pathlib import Path
import subprocess
import tempfile
import tomllib
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_NATIVE_TEST") and os.environ.get("KIRBY_LAUNCHER_TEST"),
                     "Set KIRBY_NATIVE_TEST and KIRBY_LAUNCHER_TEST")
class BuildIdentityTests(unittest.TestCase):
    def test_launcher_queries_the_actual_sibling_without_game_or_display_startup(self):
        launcher = str(Path(os.environ["KIRBY_LAUNCHER_TEST"]).resolve())
        native = str(Path(os.environ["KIRBY_NATIVE_TEST"]).resolve())
        with tempfile.TemporaryDirectory(prefix="kirby's native query ") as directory:
            result = subprocess.run([launcher, "--check-native-build", native], cwd=directory,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(tomllib.loads(result.stdout)["program"], "runtime")
            legacy = Path(directory) / "legacy executable's name"
            legacy.write_text("#!/bin/sh\nprintf 'Unsupported build information\\n' >&2\nexit 2\n")
            legacy.chmod(0o700)
            result = subprocess.run([launcher, "--check-native-build", str(legacy)], cwd=directory,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("exited with code 2", result.stderr)
            self.assertNotIn("[build]", result.stdout)

    def test_both_binaries_report_their_own_compiled_identity_without_startup(self):
        records = []
        with tempfile.TemporaryDirectory(prefix="kirby's read-only build info ") as directory:
            root = Path(directory)
            settings = root / "local/user/settings.toml"
            settings.parent.mkdir(parents=True)
            settings.write_text("[invalid settings on purpose")
            env = dict(os.environ, SDL_VIDEO_DRIVER="not-a-driver", SDL_AUDIO_DRIVER="not-a-driver")
            for variable, program in (("KIRBY_NATIVE_TEST", "runtime"), ("KIRBY_LAUNCHER_TEST", "launcher")):
                result = subprocess.run([str(Path(os.environ[variable]).resolve()), "--build-info"],
                                        cwd=root, env=env, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                record = tomllib.loads(result.stdout)
                self.assertEqual(record["schema_version"], 1)
                self.assertEqual(record["program"], program)
                self.assertEqual(record["build"]["game_id"], "RK5E01")
                self.assertEqual(record["build"]["game_revision"], 0)
                self.assertRegex(record["build"]["source_revision"], r"^[0-9a-f]{40}$")
                self.assertRegex(record["build"]["source_sha256"], r"^[0-9a-f]{64}$")
                self.assertIsInstance(record["build"]["source_modified"], bool)
                records.append(record["build"])
                self.assertNotIn("translated entry resolved", result.stdout + result.stderr)
            self.assertEqual(records[0], records[1])
            self.assertEqual(settings.read_text(), "[invalid settings on purpose")
            self.assertEqual(sorted(str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()),
                             ["local/user/settings.toml"])
