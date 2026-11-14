# SPDX-License-Identifier: GPL-3.0-only
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import kirby
from installer.jobs import Cancelled, Job, run_process
from installer.sources import archive_member, inspect_source
from tooling.test_install_sources import make_data

ROOT = Path(__file__).resolve().parents[2]


class SetupJobsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kirby's job ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.events = []

    def job(self, name="test"):
        return Job(self.root / "setup", name, self.events.append)

    def test_duplicate_jobs_are_rejected_without_overwriting_logs(self):
        with self.job() as first:
            first.log.write(b"original log\n")
            first.log.flush()
            with self.assertRaisesRegex(ValueError, "running"):
                with self.job("second"):
                    self.fail("A concurrent job acquired the setup workspace")
        with self.assertRaises(FileExistsError):
            with self.job():
                self.fail("A job overwrote an old log")
        self.assertEqual((self.root / "setup/logs/test.log").read_bytes(), b"original log\n")

    def test_process_logs_cannot_contaminate_protocol_or_exceed_capture_limit(self):
        with self.job() as job:
            output = run_process(job, [sys.executable, "-c", "print('tool output')"], capture_limit=1024)
            self.assertEqual(output, b"tool output\n")
            with self.assertRaisesRegex(ValueError, "output limit"):
                run_process(job, [sys.executable, "-c", "print('x'*4096)"], capture_limit=64)
        self.assertIn(b"tool output\n", (self.root / "setup/logs/test.log").read_bytes())
        self.assertEqual(self.events, [])

    def test_cancellation_terminates_owned_process_group(self):
        child_pid = self.root / "child.pid"
        script = ("import os,signal,time; p=os.fork(); "
                  f"open({str(child_pid)!r},'w').write(str(p if p else os.getpid())); "
                  "signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30)")
        with self.job() as job:
            timer = threading.Timer(0.5, job.cancel.set)
            timer.start()
            started = time.monotonic()
            with self.assertRaises(Cancelled):
                run_process(job, [sys.executable, "-c", script])
            timer.join()
            self.assertLess(time.monotonic() - started, 5)
        pid = int(child_pid.read_text())
        # SIGKILL delivery is asynchronous when the host is busy compiling.
        # Allow the scheduler to deliver it; a reparented zombie is acceptable.
        status = Path(f"/proc/{pid}/stat")
        deadline = time.monotonic() + 1
        while status.exists():
            try:
                state = status.read_text().split(") ", 1)[1][0]
            except FileNotFoundError:
                break
            if state == "Z":
                break
            if time.monotonic() >= deadline:
                self.fail(f"Owned child {pid} is still running after cancellation: {state}")
            time.sleep(0.01)

    def test_image_cache_is_bound_to_selected_bytes_and_revalidated(self):
        data = self.root / "fixture/DATA"
        digest = make_data(data)
        tools = self.root / "tools"
        tools.mkdir()
        count = self.root / "calls"
        tool = tools / "dolphin-tool"
        tool.write_text(f"#!{sys.executable}\nimport pathlib,shutil,sys\n"
                        f"p=pathlib.Path({str(count)!r}); p.write_text(p.read_text()+'x' if p.exists() else 'x')\n"
                        "out=pathlib.Path(sys.argv[sys.argv.index('-o')+1])\n"
                        f"shutil.copytree({str(data)!r},out/'DATA')\n")
        tool.chmod(0o700)
        image = self.root / "selected image's name.wbfs"
        image.write_bytes(b"synthetic image one")
        with patch.dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"]), patch.object(kirby, "EXPECTED_SHA1", digest):
            with self.job("first") as job:
                first = inspect_source(image, job)
            with self.job("second") as job:
                second = inspect_source(image, job)
            self.assertEqual(first["data_root"], second["data_root"])
            self.assertEqual(count.read_text(), "x")
            (Path(first["data_root"]) / "files/level/test").unlink()
            with self.job("third") as job, self.assertRaises((OSError, ValueError)):
                inspect_source(image, job)
            image.write_bytes(b"synthetic image two")
            with self.job("fourth") as job:
                fourth = inspect_source(image, job)
            self.assertNotEqual(first["source_sha256"], fourth["source_sha256"])
            self.assertEqual(count.read_text(), "xx")

    @unittest.skipUnless(shutil.which("7z"), "7z required")
    def test_corrupt_archive_does_not_reuse_unrelated_existing_data(self):
        archive = self.root / "corrupt.7z"
        archive.write_bytes(b"not an archive")
        with self.job() as job, self.assertRaises(ValueError):
            inspect_source(archive, job)
        self.assertFalse(any((self.root / "setup/staging").iterdir()))

    def test_worker_emits_json_failure_without_touching_user_files(self):
        sentinel = self.root / "save"
        sentinel.write_bytes(b"keep")
        request = dict(version=1, id="request-1", operation="inspect", source=str(self.root / "missing.wbfs"))
        result = subprocess.run([sys.executable, str(ROOT / "tools/install.py"), "--workspace", str(self.root / "setup")],
                                input=json.dumps(request) + "\n", capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[-1]["state"], "failed")
        self.assertTrue(all(event["job_id"] == "request-1" and event["version"] == 1 for event in events))
        self.assertEqual([event["sequence"] for event in events], list(range(1, len(events) + 1)))
        self.assertEqual(sentinel.read_bytes(), b"keep")
        self.assertEqual(result.stderr, "")

    def test_worker_rejects_unsupported_operations_and_malformed_requests(self):
        for request in ('{', json.dumps(dict(id="../unsafe", operation="inspect", source="x")),
                        json.dumps(dict(id="job", operation="unsupported", source="x"))):
            result = subprocess.run([sys.executable, str(ROOT / "tools/install.py"), "--workspace", str(self.root / "setup")],
                                    input=request + "\n", capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            event = json.loads(result.stdout.splitlines()[-1])
            self.assertEqual(event["state"], "failed")
            self.assertFalse((self.root / "setup/sources").exists())

    def test_worker_accepts_immediate_cancel_and_exits_with_stdin_still_open(self):
        image = self.root / "source.wbfs"
        image.write_bytes(b"synthetic")
        process = subprocess.Popen([sys.executable, str(ROOT / "tools/install.py"), "--workspace", str(self.root / "setup")],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            process.stdin.write(json.dumps(dict(id="cancel-me", operation="inspect", source=str(image))) + "\n"
                                + json.dumps(dict(id="cancel-me", operation="cancel")) + "\n")
            process.stdin.flush()
            process.wait(timeout=5)  # A launcher keeps its control pipe open.
            events = [json.loads(line) for line in process.stdout.read().splitlines()]
            self.assertEqual(process.returncode, 2, events)
            self.assertEqual(events[-1]["state"], "cancelled")
            self.assertEqual(process.stderr.read(), "")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdin.close()
            process.stdout.close()
            process.stderr.close()

    def test_failed_worker_exits_cleanly_when_launcher_keeps_control_pipe_open(self):
        process = subprocess.Popen([sys.executable, str(ROOT / "tools/install.py"), "--workspace", str(self.root / "setup")],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            process.stdin.write(json.dumps(dict(id="missing", operation="inspect", source=str(self.root / "missing.wbfs"))) + "\n")
            process.stdin.flush()
            process.wait(timeout=5)
            self.assertEqual(process.returncode, 1, process.stderr.read())
            self.assertEqual(process.stderr.read(), "")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdin.close()
            process.stdout.close()
            process.stderr.close()

    def test_archive_rejects_multiple_images_and_unsafe_member_names(self):
        for name in ("../escape.wbfs", "/absolute.wbfs", "folder\\escape.wbfs"):
            with self.assertRaises(ValueError):
                archive_member(f"Path = {name}\nSize = 100\nEncrypted = -\n")
        with self.assertRaises(ValueError):
            archive_member("Path = a.wbfs\nSize = 100\n\nPath = b.wbfs\nSize = 100\n")
        with self.assertRaises(ValueError):
            archive_member("Path = link.wbfs\nSize = 100\nAttributes = A lrwxrwxrwx\n")

    @unittest.skipUnless(shutil.which("7z"), "7z required")
    def test_archive_import_uses_literal_member_name_and_checks_actual_payload(self):
        data = self.root / "fixture/DATA"
        digest = make_data(data)
        tools = self.root / "tools"
        tools.mkdir()
        tool = tools / "dolphin-tool"
        tool.write_text(f"#!{sys.executable}\nimport pathlib,shutil,sys\n"
                        "out=pathlib.Path(sys.argv[sys.argv.index('-o')+1])\n"
                        f"shutil.copytree({str(data)!r},out/'DATA')\n")
        tool.chmod(0o700)
        inputs = self.root / "inputs"
        inputs.mkdir()
        name = "Kirby's [literal] image.wbfs"
        (inputs / name).write_bytes(b"synthetic image")
        (inputs / "README.txt").write_text("extra archive member")
        archive = self.root / "selected archive's name.7z"
        subprocess.run([shutil.which("7z"), "a", str(archive), name, "README.txt"], cwd=inputs,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
        with patch.dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"]), patch.object(kirby, "EXPECTED_SHA1", digest):
            with self.job() as job:
                result = inspect_source(archive, job)
            self.assertTrue((Path(result["data_root"]) / "files/level/test").is_file())
            self.assertFalse(any((self.root / "setup/staging").iterdir()))
            # Change a packed payload byte while retaining a valid archive header.
            packed = bytearray(archive.read_bytes())
            packed[35] ^= 0xFF
            archive.write_bytes(packed)
            with self.job("corrupt") as job, self.assertRaises(ValueError):
                inspect_source(archive, job)

    def test_low_disk_space_leaves_no_partial_cache_or_user_changes(self):
        image = self.root / "source.wbfs"
        image.write_bytes(b"synthetic")
        sentinel = self.root / "user-save"
        sentinel.write_bytes(b"keep")
        usage = shutil.disk_usage(self.root)._replace(free=1)
        with self.job() as job, patch("installer.sources.shutil.disk_usage", return_value=usage):
            with self.assertRaisesRegex(ValueError, "free space"):
                inspect_source(image, job)
        self.assertEqual(sentinel.read_bytes(), b"keep")
        self.assertFalse((self.root / "setup/sources").exists())
        self.assertFalse(any((self.root / "setup/staging").iterdir()))

    def test_cancelled_extraction_removes_staging_and_keeps_existing_data(self):
        tools = self.root / "tools"
        tools.mkdir()
        tool = tools / "dolphin-tool"
        marker = self.root / "tool-started"
        tool.write_text(f"#!{sys.executable}\nimport pathlib,time\n"
                        f"pathlib.Path({str(marker)!r}).write_text('started')\ntime.sleep(30)\n")
        tool.chmod(0o700)
        image = self.root / "source.wbfs"
        image.write_bytes(b"synthetic image")
        sentinel = self.root / "installed-game"
        sentinel.write_bytes(b"keep")
        with patch.dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"]):
            with self.job() as job:
                timer = threading.Timer(0.3, job.cancel.set)
                timer.start()
                try:
                    with self.assertRaises(Cancelled):
                        inspect_source(image, job)
                finally:
                    timer.join()
        self.assertTrue(marker.is_file())
        self.assertFalse(any((self.root / "setup/staging").iterdir()))
        self.assertFalse((self.root / "setup/sources").exists())
        self.assertEqual(sentinel.read_bytes(), b"keep")

    @unittest.skipIf(os.geteuid() == 0, "Root bypasses workspace permissions")
    def test_read_only_workspace_fails_without_creating_artifacts(self):
        workspace = self.root / "read-only"
        workspace.mkdir(mode=0o500)
        try:
            with self.assertRaises(PermissionError):
                with Job(workspace, "job", self.events.append):
                    self.fail("Read-only workspace accepted a setup job")
            self.assertEqual(list(workspace.iterdir()), [])
        finally:
            workspace.chmod(0o700)
