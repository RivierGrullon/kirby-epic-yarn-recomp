# SPDX-License-Identifier: GPL-3.0-only
"""Presentation feedback is an opt-in check on an unlocked Wayland desktop."""
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('KIRBY_WAYLAND_PROTOCOL_TEST'),
    'Set KIRBY_WAYLAND_PROTOCOL_TEST for an unmapped Wayland protocol probe')
class WaylandProtocolTests(unittest.TestCase):
    def test_real_registry_clock_and_hidden_surface_cleanup(self):
        program=Path(os.environ['KIRBY_WAYLAND_PROTOCOL_TEST']).resolve()
        with tempfile.TemporaryDirectory(prefix='kirby-wayland-protocol-') as directory:
            path=Path(directory)/'feedback.csv'
            result=subprocess.run([program,path],cwd=directory,capture_output=True,text=True,
                env=dict(os.environ,SDL_AUDIO_DRIVER='dummy',SDL_VIDEODRIVER='wayland'),timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('no buffer was presented',result.stdout)
            with path.open() as source:rows=list(csv.DictReader(source))
            self.assertEqual([row['event'] for row in rows],['clock','pending'])
            self.assertGreater(int(rows[0]['clock_ns']),0)
            self.assertGreaterEqual(int(rows[0]['sdl_after_ns']),int(rows[0]['sdl_before_ns']))
            self.assertEqual(rows[1]['request'],'1')


@unittest.skipUnless(os.environ.get('KIRBY_NATIVE_TEST'),'Set KIRBY_NATIVE_TEST')
class PresentationTraceArgumentTests(unittest.TestCase):
    def test_feedback_capture_requires_visible_video_before_creating_output(self):
        program=Path(os.environ['KIRBY_NATIVE_TEST']).resolve()
        with tempfile.TemporaryDirectory(prefix='kirby-presentation-flags-') as directory:
            path=Path(directory)/'feedback.csv'
            for mode in ('--probe-init','--video-hidden'):
                result=subprocess.run([program,mode,'--presentation-log',path,'--volume','0'],
                    env=dict(os.environ,SDL_AUDIO_DRIVER='dummy'),capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                self.assertIn('--presentation-log requires --video',result.stderr)
                self.assertFalse(path.exists())


@unittest.skipUnless(os.environ.get('KIRBY_NATIVE_PRESENTATION_TEST'),
    'Set KIRBY_NATIVE_PRESENTATION_TEST for a visible Wayland window')
class NativePresentationTraceTests(unittest.TestCase):
    def test_compositor_reports_real_presentation_feedback_and_clock(self):
        program=Path(os.environ['KIRBY_NATIVE_PRESENTATION_TEST']).resolve()
        lock=subprocess.check_output(['loginctl','show-session',os.environ['XDG_SESSION_ID'],
            '-p','LockedHint','--value'],text=True).strip()
        if lock!='no' or os.environ.get('XDG_SESSION_TYPE')!='wayland':
            self.skipTest('Requires an unlocked Wayland desktop')
        data=Path('disc/DATA').resolve()
        with tempfile.TemporaryDirectory(prefix='kirby-native-feedback-') as directory:
            path=Path(directory)/'feedback.csv';log=Path(directory)/'native.log'
            with log.open('wb') as output:
                process=subprocess.Popen([program,'--video','--volume','0','--game-data',data,
                    '--presentation-log',path],cwd=directory,env=dict(os.environ,SDL_AUDIO_DRIVER='dummy'),
                    stdout=output,stderr=subprocess.STDOUT)
                try:
                    deadline=time.monotonic()+40
                    while time.monotonic()<deadline:
                        self.assertIsNone(process.poll(),log.read_text(errors='replace'))
                        if path.exists():
                            with path.open() as source:rows=list(csv.DictReader(source))
                            if sum(row.get('event')=='presented' for row in rows)>=4:break
                        time.sleep(.05)
                    else:self.fail('No actual Wayland feedback: '+log.read_text(errors='replace'))
                    process.terminate();result=process.wait(timeout=10)
                finally:
                    if process.poll() is None:process.kill()
                    process.wait(timeout=5)
            self.assertEqual(result,0,log.read_text(errors='replace'))
            with path.open() as source:rows=list(csv.DictReader(source))
            clocks=[row for row in rows if row['event']=='clock']
            self.assertEqual(len(clocks),1)
            clock=clocks[0]
            self.assertGreater(int(clock['clock_ns']),0)
            self.assertGreaterEqual(int(clock['sdl_after_ns']),int(clock['sdl_before_ns']))
            presented=[row for row in rows if row['event']=='presented']
            self.assertGreaterEqual(len(presented),4)
            for row in presented:
                self.assertGreater(int(row['present_ns']),0)
                self.assertGreaterEqual(int(row['callback_sdl_ns']),int(row['request_sdl_ns']))
                self.assertGreater(int(row['request']),0)
                self.assertLess(int(row['flags']),16)
