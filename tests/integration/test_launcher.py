"""Opt-in checks for the native frontend, using the local build only."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_LAUNCHER_TEST"), "Set KIRBY_LAUNCHER_TEST to the native launcher")
class LauncherTests(unittest.TestCase):
    def invoke(self, *arguments, **kwargs):
        return subprocess.run([os.environ["KIRBY_LAUNCHER_TEST"], *arguments],
                              capture_output=True, text=True, timeout=15, **kwargs)

    def test_real_dump_is_verified_without_starting_the_game(self):
        result = self.invoke("--check-data", str(Path("disc/DATA").resolve()))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("RK5E01 Rev 0 (USA) verified", result.stdout)

    def test_wrong_dol_is_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for entry in ("sys/main.dol", "sys/boot.bin", "sys/fst.bin", "ticket.bin", "tmd.bin"):
                path = root / entry
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"synthetic wrong revision")
            (root / "files").mkdir()
            result = self.invoke("--check-data", str(root))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("verification failed", result.stdout)

    def test_incomplete_data_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.invoke("--check-data", directory)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Missing sys/main.dol", result.stdout)

    def test_frontend_renders_and_exits_without_showing_a_window(self):
        env = os.environ.copy()
        env.update(SDL_VIDEO_DRIVER="offscreen", SDL_RENDER_DRIVER="software")
        result = self.invoke("--self-test", env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
