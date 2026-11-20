"""Opt-in checks for the native frontend, using the local build only."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_LAUNCHER_TEST"), "Set KIRBY_LAUNCHER_TEST to the native launcher")
class LauncherTests(unittest.TestCase):
    def invoke(self, *arguments, **kwargs):
        return subprocess.run([os.environ["KIRBY_LAUNCHER_TEST"], *arguments],
                              capture_output=True, encoding="utf-8", timeout=15, **kwargs)

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

    def test_native_setup_client_inspects_the_actual_selected_data(self):
        source=Path("disc/DATA").resolve()
        result=self.invoke("--check-source",str(source))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("RK5E01 Rev 0 game data verified",result.stdout)
        self.assertIn(str(source),result.stdout)
        with tempfile.TemporaryDirectory(prefix="kirby's unrelated source ") as directory:
            result=self.invoke("--check-source",directory)
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        self.assertNotIn("game data verified",result.stdout)

    def test_frontend_renders_and_exits_without_showing_a_window(self):
        env = os.environ.copy()
        env.update(SDL_VIDEO_DRIVER="offscreen", SDL_RENDER_DRIVER="software")
        result = self.invoke("--self-test", env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_qt_host_persists_language_settings_and_key_capture(self):
        binary = Path(os.environ["KIRBY_LAUNCHER_TEST"]).resolve()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copy2(binary, root/binary.name)
            (root/"kirby-portable.txt").touch()
            env = os.environ.copy()
            env["PATH"] = str(binary.parent) + os.pathsep + env["PATH"]
            env["LD_LIBRARY_PATH"] = str(binary.parent.parent.parent/"local/host-libs") + os.pathsep + env.get("LD_LIBRARY_PATH", "")

            def session(commands):
                records = [dict(focused=True, controls=True, **command) for command in commands]
                records.append({"cmd": "quit"})
                result = subprocess.run([root/binary.name, "--ui-service"], cwd=root, env=env,
                                        input="".join(json.dumps(r)+"\n" for r in records),
                                        capture_output=True, encoding="utf-8", timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                replies = [json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(len(replies), len(commands))
                self.assertFalse(any("error" in r for r in replies), replies)
                return replies

            replies = session([{"cmd":"hello"}, {"cmd":"lang","value":2},
                               {"cmd":"change","key":"volume","value":37},
                               {"cmd":"bind","action":4,"keyboard":True},
                               {"cmd":"key","key":"F10","down":True},
                               {"cmd":"key","key":"F10","down":False}, {"cmd":"save"}])
            self.assertEqual(replies[0]["state"]["lang"], 0)
            self.assertTrue(replies[3]["input"]["keyboardCapture"])
            state = session([{"cmd":"hello"}])[0]["state"]
            self.assertEqual((state["lang"],state["config"]["volume"],state["bindings"][4][2]), (2,37,"F10"))
