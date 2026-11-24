# SPDX-License-Identifier: GPL-3.0-only
"""Exclusive setup jobs and cancellable, shell-free external tools."""
import os
import math
from pathlib import Path
import re
import signal
import subprocess
import threading
import time

from .platforms import WINDOWS, is_link, readable, open_lock

if not WINDOWS:
    import fcntl


def _lock_windows(fd):
    """Lock byte zero using native errors; CRT locking can lose GetLastError."""
    import ctypes
    from ctypes import wintypes
    import msvcrt
    class Overlapped(ctypes.Structure):
        _fields_=[('internal',ctypes.c_size_t),('internal_high',ctypes.c_size_t),
                  ('offset',wintypes.DWORD),('offset_high',wintypes.DWORD),('event',wintypes.HANDLE)]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.LockFileEx.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.DWORD,
                               wintypes.DWORD,wintypes.DWORD,ctypes.POINTER(Overlapped)]
    kernel.LockFileEx.restype=wintypes.BOOL
    position=Overlapped()
    if not kernel.LockFileEx(msvcrt.get_osfhandle(fd),3,0,1,0,ctypes.byref(position)):
        error=ctypes.get_last_error()
        if error==33:  # ERROR_LOCK_VIOLATION
            raise ValueError('Another setup job is running')
        raise ctypes.WinError(error)


class Cancelled(Exception):
    pass


class Job:
    def __init__(self, workspace, identifier, emit):
        if not isinstance(identifier, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", identifier):
            raise ValueError("Job ID must contain 1-80 letters, digits, underscores or hyphens")
        self.workspace = Path(workspace).absolute()
        self.identifier = identifier
        self.emit = emit
        self.cancel = threading.Event()
        self.committed = False
        self.lock_fd = None
        self.log = None
        self.log_path = self.workspace / "logs" / f"{identifier}.log"

    def __enter__(self):
        self.workspace.mkdir(parents=True, exist_ok=True, mode=0o700)
        if is_link(self.workspace):
            raise ValueError("Setup workspace must not be a symlink")
        lock_path = self.workspace / "job.lock"
        if is_link(lock_path):
            raise ValueError("Setup lock must not be a link")
        self.lock_fd = open_lock(lock_path)
        try:
            if WINDOWS:
                _lock_windows(self.lock_fd)
            else:
                try:
                    fcntl.flock(self.lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise ValueError("Another setup job is running") from None
            for name in ("logs", "staging"):
                path = self.workspace / name
                path.mkdir(exist_ok=True, mode=0o700)
                if is_link(path):
                    raise ValueError("Setup directories must not be symlinks")
            fd = os.open(self.log_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_BINARY", 0), 0o600)
            self.log = os.fdopen(fd, "wb")
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        if self.log is not None:
            self.log.close()
        if self.lock_fd is not None:
            os.close(self.lock_fd)
            self.lock_fd = None

    def check_cancel(self):
        if self.cancel.is_set() and not self.committed:
            raise Cancelled("Setup cancelled; installed game and saves were left untouched")

    def write_log(self, data):
        if self.log.tell() + len(data) > 64 * 1024 * 1024:
            raise ValueError("Setup tool log exceeded the 64 MiB limit")
        self.log.write(data)
        self.log.flush()


def run_process(job, command, *, capture_limit=0,timeout=None,cancellable=True,log_output=True,
                env=None,cwd=None,on_output=None,stop_when=None,stop_timeout=5):
    """Log output and clean the owned process group on failure.

    With stop_when, timeout bounds startup and stop_timeout separately bounds
    shutdown, including EOF from any inherited output pipes. Readiness
    requests shutdown; only a zero process exit status can complete the probe.
    """
    if stop_when is not None and (not isinstance(stop_timeout,(int,float)) or
                                 not math.isfinite(stop_timeout) or stop_timeout<=0):
        raise ValueError('Shutdown timeout must be finite and positive')
    started=time.monotonic()
    process=None
    stopping=None
    tree=None
    def check(*,request_stop=True):
        nonlocal stopping
        if cancellable:
            job.check_cancel()
        now=time.monotonic()
        if stopping is not None:
            if now-stopping>=stop_timeout:
                message=(f'Native startup probe did not stop after verification '
                         f'(shutdown exceeded {stop_timeout:g} seconds); see {job.log_path}')
                if log_output:
                    job.write_log(f'[setup] {message}\n'.encode())
                raise TimeoutError(message)
            return
        if timeout is not None and now-started>=timeout:
            raise TimeoutError(f"{Path(command[0]).name} timed out after {timeout} seconds")
        if request_stop and process is not None and stop_when is not None:
            if stop_when():
                stopping=now
                if log_output:
                    job.write_log(f'[setup] Native startup verified; requesting shutdown '
                                  f'(deadline {stop_timeout:g} seconds)\n'.encode())
                if WINDOWS:
                    tree.stop()
                else:
                    try:os.killpg(process.pid,signal.SIGTERM)
                    except ProcessLookupError:pass
    check()
    if WINDOWS:
        from .windows_process import ProcessTree
        tree=ProcessTree()
    captured = bytearray()
    try:
        process = subprocess.Popen([os.fspath(arg) for arg in command], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, env=env, cwd=cwd,
                                   **({'creationflags':0x204} if WINDOWS else {'start_new_session':True}))
        if tree is not None:
            tree.start(process)
        while True:
            check()
            if not readable(process.stdout.fileno()):
                continue
            data = os.read(process.stdout.fileno(), 64 * 1024)
            if not data:
                break
            if log_output:
                job.write_log(data)
            if on_output is not None:
                on_output(data)
            if capture_limit:
                if len(captured) + len(data) > capture_limit:
                    raise ValueError("Setup tool output limit exceeded")
                captured.extend(data)
        while process.poll() is None:
            check()
            time.sleep(0.05)
        # The final read/wait may itself cross a deadline. Recheck it without
        # starting a new shutdown after the child has already been reaped.
        check(request_stop=False)
        if process.returncode:
            raise ValueError(f"{Path(command[0]).name} exited with code {process.returncode}; see {job.log_path}")
        if stopping is not None and log_output:
            job.write_log(b'[setup] Native startup probe exited successfully after verification\n')
        return bytes(captured)
    except BaseException:
        if process is not None:
            if WINDOWS:
                # Also terminate an unassigned suspended process if assignment failed.
                tree.stop()
                if process.poll() is None:
                    process.kill()
                process.wait()
            else:
                # Keep the leader unreaped until signalling, including when a tool forks.
                if stopping is None:
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        process.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        pass
                # A verified probe already received SIGTERM. Do not re-enter its
                # cleanup handler or extend its grace period after a failure.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        raise
    finally:
        if tree is not None:
            tree.close()
        if process is not None:
            process.stdout.close()
