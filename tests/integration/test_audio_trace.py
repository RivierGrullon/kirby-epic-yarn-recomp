# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in real DMA capture; SDL dummy output and zero gain remain mandatory."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest
import wave


@unittest.skipUnless(os.environ.get('KIRBY_NATIVE_AUDIO_TEST'),'Set KIRBY_NATIVE_AUDIO_TEST to the capture-enabled native program')
class NativeAudioTraceTests(unittest.TestCase):
    def setUp(self):
        self.program=Path(os.environ['KIRBY_NATIVE_AUDIO_TEST']).resolve()
        self.data=Path('disc/DATA').resolve()
        self.environment=dict(os.environ,SDL_AUDIO_DRIVER='dummy')

    def test_capture_requires_video_and_preserves_existing_output(self):
        with tempfile.TemporaryDirectory(prefix="kirby's audio flags ") as directory:
            path=Path(directory)/'capture.wav'
            result=subprocess.run([self.program,'--probe-init','--audio-log',path,'--volume','0'],
                cwd=directory,env=self.environment,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,2,result.stdout+result.stderr)
            self.assertIn('--audio-log requires',result.stderr)
            self.assertFalse(path.exists())
            path.write_bytes(b'keep previous capture')
            result=subprocess.run([self.program,'--video-hidden','--audio-log',path,'--volume','0','--game-data',self.data],
                cwd=directory,env=self.environment,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,1,result.stdout+result.stderr)
            self.assertIn('Cannot create PCM trace',result.stderr)
            self.assertEqual(path.read_bytes(),b'keep previous capture')

    def test_real_dma_capture_finishes_as_wav_with_monotonic_clock_checkpoints(self):
        with tempfile.TemporaryDirectory(prefix='kirby-audio-source-') as directory:
            path=Path(directory)/'source.wav';log=Path(directory)/'native.log'
            with log.open('wb') as output:
                process=subprocess.Popen([self.program,'--video-hidden','--volume','0','--audio-log',path,
                    '--game-data',self.data],cwd=directory,env=self.environment,stdout=output,stderr=subprocess.STDOUT)
                try:
                    deadline=time.monotonic()+40
                    while time.monotonic()<deadline:
                        self.assertIsNone(process.poll(),log.read_text(errors='replace'))
                        if path.exists() and path.stat().st_size>65536:break
                        time.sleep(.05)
                    else:self.fail('Audio capture did not receive DMA samples: '+log.read_text(errors='replace'))
                    process.terminate();result=process.wait(timeout=10)
                finally:
                    if process.poll() is None:process.kill()
                    process.wait(timeout=5)
            self.assertEqual(result,0,log.read_text(errors='replace'))
            with wave.open(str(path),'rb') as audio:
                self.assertEqual((audio.getnchannels(),audio.getsampwidth(),audio.getframerate()),(2,2,32000))
                frames=audio.getnframes();self.assertGreater(frames,4096)
                self.assertEqual(len(audio.readframes(frames)),frames*4)
            data=path.read_bytes();offset=44+frames*4
            self.assertEqual(data[offset:offset+4],b'kTIM')
            length,version,stride=struct.unpack_from('<III',data,offset+4)
            self.assertEqual((version,stride),(1,16))
            self.assertEqual(len(data),offset+8+length)
            stamps=[struct.unpack_from('<QQ',data,index) for index in range(offset+16,len(data),16)]
            self.assertGreaterEqual(len(stamps),2);self.assertEqual(stamps[0][0],0)
            self.assertEqual(stamps,sorted(stamps))
            for first,second in zip(stamps,stamps[1:]):
                self.assertGreaterEqual(second[0]-first[0],4096)
                self.assertGreaterEqual(second[1],first[1])
            self.assertLess(stamps[-1][0],frames)
