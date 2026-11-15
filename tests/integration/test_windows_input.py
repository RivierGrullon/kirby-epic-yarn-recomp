"""Windows startup rejection tests using synthetic headers and an empty DOL."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == "win32" and os.environ.get("KIRBY_NATIVE_TEST"),
                     "Set KIRBY_NATIVE_TEST to test Windows input rejection")
class WindowsInputTests(unittest.TestCase):
    def test_unsupported_inputs_stop_before_guest_execution(self):
        executable = Path(os.environ["KIRBY_NATIVE_TEST"]).resolve()
        private = Path(__file__).resolve().parents[2] / "local/windows-input-tests"
        private.mkdir(parents=True, exist_ok=True)
        cases = (
            ("game ID", b"ZZZE01\x00\x00", 0, "Only RK5E01 Rev 0"),
            ("revision", b"RK5E01\x00\x01", 0, "Only RK5E01 Rev 0"),
            ("size", b"RK5E01\x00\x00", 16, "Supported DOL size mismatch"),
            ("hash", b"RK5E01\x00\x00", 9340736, "Unsupported DOL SHA-256"),
        )
        with tempfile.TemporaryDirectory(dir=private) as directory:
            root = Path(directory)
            (root / "sys").mkdir()
            for name, boot, size, message in cases:
                with self.subTest(name=name):
                    (root / "sys/boot.bin").write_bytes(boot)
                    with (root / "sys/main.dol").open("wb") as dol:
                        dol.truncate(size)
                    result = subprocess.run(
                        [str(executable), "--probe-init", "--game-data", str(root)],
                        cwd=root, capture_output=True, text=True, timeout=15)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn(message, result.stderr)
                    self.assertNotIn("entering __start", result.stdout + result.stderr)
