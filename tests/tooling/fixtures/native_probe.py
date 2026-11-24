# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic native executable for the real installer verification pipeline."""
import json
import os
from pathlib import Path
import signal
import sys
import time


scenario = os.environ['KIRBY_TEST_PROBE_SCENARIO']
record = Path(os.environ['KIRBY_TEST_PROBE_RECORD'])
mode = sys.argv[1]
with record.open('a') as output:
    output.write(json.dumps(dict(mode=mode, cwd=os.getcwd(), args=sys.argv[2:],
                                audio=os.environ.get('SDL_AUDIO_DRIVER'))) + '\n')

if mode == '--build-info':
    print(os.environ['KIRBY_TEST_BUILD_INFO'])
    sys.exit(0)
if scenario == mode:
    sys.exit(17)
if mode != '--video-hidden':
    sys.exit(0)
if scenario == 'early-failure':
    sys.exit(19)


def stop(*_):
    # A second SIGTERM during cleanup must not turn a stuck process into a pass.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    Path(os.environ['KIRBY_TEST_STOP_RECORD']).write_text('SIGTERM')
    print('host exit requested; shutting down native runtime', flush=True)
    if scenario == 'slow-clean':
        time.sleep(6)
    if scenario == 'shutdown-failure':
        # Match the runtime's handled SIGSEGV path: logged signal, exit status 1.
        print('Signal 11 (fault address 0x280)', flush=True)
        sys.exit(1)
    if scenario == 'unhandled-signal':
        signal.signal(signal.SIGTERM, signal.SIG_DFL)
        os.kill(os.getpid(), signal.SIGTERM)
    print('synthetic shutdown complete', flush=True)
    sys.exit(0)


signal.signal(signal.SIGTERM, stop)
markers = ('WPAD initialized', 'verified AXWii task loaded', 'first GX draw submitted')
for index, marker in enumerate(markers):
    if scenario != f'missing-marker-{index}':
        print(marker, flush=True)
frames = Path(sys.argv[sys.argv.index('--frame-log') + 1])
frames.write_text('sequence,vi_field\n' + ('1,1\n' if scenario != 'no-frame' else ''))
if scenario == 'no-frame' or scenario.startswith('missing-marker-'):
    sys.exit(0)
time.sleep(20)
sys.exit(21)
