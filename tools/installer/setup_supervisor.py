# SPDX-License-Identifier: GPL-3.0-only
"""Linux subreaper: a crashed worker cannot orphan extraction/build processes."""
import ctypes
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def supervise(command):
    if sys.platform!='linux':return subprocess.call(command)
    libc=ctypes.CDLL(None,use_errno=True)
    if libc.prctl(36,1,0,0,0):  # PR_SET_CHILD_SUBREAPER, before the worker can fork.
        raise OSError(ctypes.get_errno(),'Could not supervise setup processes')
    child=subprocess.Popen(command)
    previous={}
    def forward(number, frame):
        if child.poll() is None:
            try:child.send_signal(number)
            except ProcessLookupError:pass
    for number in (signal.SIGTERM,signal.SIGINT):previous[number]=signal.signal(number,forward)
    try:
        code=child.wait()
        # On worker exit, its surviving descendants become this process's children,
        # even when they created independent sessions. Reap them before reporting exit.
        deadline=time.monotonic()+5
        while True:
            while True:
                try:
                    pid,_=os.waitpid(-1,os.WNOHANG)
                    if pid==0:break
                except ChildProcessError:return code if code>=0 else 128-code
            children=(Path('/proc/self/task')/str(os.getpid())/'children').read_text().split()
            for pid in children:
                try:os.kill(int(pid),signal.SIGKILL)
                except ProcessLookupError:pass
            if time.monotonic()>=deadline:
                print('Could not verify setup process cleanup',file=sys.stderr,flush=True)
                return 1
            time.sleep(.02)
    finally:
        for number,handler in previous.items():signal.signal(number,handler)


if __name__=='__main__':
    arguments=sys.argv[1:]
    if arguments[:1]==['--']:arguments=arguments[1:]
    if not arguments:raise SystemExit('A worker command is required')
    raise SystemExit(supervise(arguments))
