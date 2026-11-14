#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""JSON-lines own-disc inspection and verified native installation worker."""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import sys
import threading
import uuid

from kirby import ROOT
from installer.jobs import Cancelled, Job
from installer.sources import inspect_source
from installer.pipeline import execute_install

LIMIT = 64 * 1024


class RequestReader:
    """Pollable input without a buffered stdin lock surviving interpreter shutdown."""
    def __init__(self, fd):
        self.fd = fd
        self.buffer = bytearray()

    def read(self, stop=None):
        while b"\n" not in self.buffer:
            if stop is not None and stop.is_set():
                return None
            if len(self.buffer) > LIMIT:
                raise ValueError("Oversized setup request")
            if not select.select([self.fd], [], [], 0.1)[0]:
                continue
            chunk = os.read(self.fd, min(4096, LIMIT + 1 - len(self.buffer)))
            if not chunk:
                if not self.buffer:
                    raise EOFError("Missing setup request")
                break
            self.buffer.extend(chunk)
        end = self.buffer.find(b"\n")
        end = end + 1 if end >= 0 else len(self.buffer)
        if end > LIMIT:
            raise ValueError("Oversized setup request")
        line = bytes(self.buffer[:end])
        del self.buffer[:end]
        value = json.loads(line)
        if not isinstance(value, dict) or type(value.get("version", 1)) is not int or value.get("version", 1) != 1:
            raise ValueError("Setup request must be a version 1 JSON object")
        return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=ROOT / "local/setup")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--inspect", type=Path, help="Inspect a selected source without a stdin request")
    selection.add_argument("--install", type=Path, help="Build, verify and select a native copy from this source")
    args = parser.parse_args()
    sequence = 0
    identifier = None
    operation = "inspect"
    output_lock = threading.Lock()

    def emit(event):
        nonlocal sequence
        with output_lock:
            sequence += 1
            print(json.dumps(dict(event, version=1, job_id=identifier, sequence=sequence)), flush=True)

    try:
        reader = RequestReader(sys.stdin.fileno())
        direct = args.inspect is not None or args.install is not None
        request = (dict(id=uuid.uuid4().hex, operation="install" if args.install is not None else "inspect",
                        source=str(args.install if args.install is not None else args.inspect))
                   if direct else reader.read())
        identifier = request.get("id")
        # Validate the ID before it can become a filename; entering acquires the job lock.
        job = Job(args.workspace, identifier, emit)
        operation = request.get("operation")
        if operation not in ("inspect", "install"):
            raise ValueError("Supported setup operations are inspect, install and cancellation of an active job")
        selected = request.get("source")
        if not isinstance(selected, str) or not selected or "\0" in selected:
            raise ValueError("A nonempty source path is required")
        with job:
            signal.signal(signal.SIGTERM, lambda *_: job.cancel.set())
            signal.signal(signal.SIGINT, lambda *_: job.cancel.set())

            finished = threading.Event()

            def read_control():
                while True:
                    try:
                        control = reader.read(finished)
                    except (EOFError, ValueError, UnicodeError):
                        return
                    if control is None:
                        return
                    if control.get("id") == identifier and control.get("operation") == "cancel":
                        job.cancel.set()
                        return
                    emit(dict(stage="request", state="rejected", error="Only cancellation of the active job is accepted"))

            control_thread = threading.Thread(target=read_control) if not direct else None
            try:
                if control_thread is not None:
                    control_thread.start()
                emit(dict(stage="inspect", state="started", log_path=str(job.log_path)))
                result = inspect_source(selected, job)
                if operation == "install":
                    result = execute_install(job, ROOT, result)
                finished.set()
                if control_thread is not None:
                    control_thread.join()
                job.check_cancel()
                emit(dict(stage=operation, state="complete", result=result))
            finally:
                finished.set()
                if control_thread is not None:
                    control_thread.join()
        return 0
    except Cancelled as error:
        emit(dict(stage=operation, state="cancelled", error=str(error)))
        return 2
    except (EOFError, OSError, ValueError, UnicodeError) as error:
        emit(dict(stage=operation, state="failed", error=str(error)))
        return 1


if __name__ == "__main__":
    sys.exit(main())
