# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.verification import check_elf,verify_native
from installer.jobs import Job
from kirby import EXPECTED_SHA1

ROOT = Path(__file__).resolve().parents[2]


class NativeFormatTests(unittest.TestCase):
    def test_only_native_x86_64_elf_outputs_are_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'WiiCompiled'
            header=bytearray(64);header[:6]=b'\x7fELF\x02\x01'
            struct.pack_into('<HH',header,16,3,62)
            path.write_bytes(header);check_elf(path)
            for body in (b'MZ'+bytes(62),b'\x7fELF'+bytes(60),bytes(header[:18])+b'\xb7\x00'+bytes(header[20:])):
                path.write_bytes(body)
                with self.assertRaises(ValueError):check_elf(path)


@unittest.skipIf(os.name == 'nt', 'Synthetic executable uses POSIX signals and a shebang')
class SyntheticNativeVerificationTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='kirby-verification-')))
        self.program = self.root / 'WiiCompiled'
        self.program.write_text(f'#!{sys.executable}\n' +
                                (ROOT / 'tests/tooling/fixtures/native_probe.py').read_text())
        self.program.chmod(0o700)
        self.savedata = self.root / 'local/user/saves/sentinel'
        self.savedata.parent.mkdir(parents=True)
        self.savedata.write_bytes(b'existing save data')
        self.sequence = 0

    def verify(self, scenario='clean', *, build_changes=None):
        self.sequence += 1
        record = self.root / f'calls-{self.sequence}.jsonl'
        record.touch()  # Preserve the original error if the fixture cannot launch.
        stop_record = self.root / f'stop-{self.sequence}'
        build = dict(game_id='RK5E01', game_revision=0, dol_sha1=EXPECTED_SHA1,
                     source_revision='synthetic-source', source_modified=False,
                     source_sha256='a' * 64)
        build.update(build_changes or {})
        info = 'schema_version = 1\nprogram = "runtime"\n[build]\n' + ''.join(
            f'{key} = {json.dumps(value)}\n' for key, value in build.items())
        environment = dict(os.environ, KIRBY_TEST_BUILD_INFO=info,
                           KIRBY_TEST_PROBE_SCENARIO=scenario,
                           KIRBY_TEST_PROBE_RECORD=str(record),
                           KIRBY_TEST_STOP_RECORD=str(stop_record))
        self.events = []
        with Job(self.root / 'setup', f'synthetic-{self.sequence}', self.events.append) as job:
            # The format gate is tested separately; everything after it uses the
            # real verifier, real processes, real files, and real SIGTERM delivery.
            with patch('installer.verification.check_elf'), patch.dict(os.environ, environment):
                try:
                    return verify_native(job, self.root, self.program, self.root / 'DATA',
                                         dict(source_revision='synthetic-source', source_modified=False,
                                              source_sha256='a' * 64))
                finally:
                    self.log = job.log_path.read_text()
                    self.calls = [json.loads(line) for line in record.read_text().splitlines()]
                    self.stopped = stop_record.exists()
                    self.assertEqual(list((job.workspace / 'staging').iterdir()), [])
                    self.assertEqual(self.savedata.read_bytes(), b'existing save data')

    def test_verified_frame_and_clean_exit_pass_in_isolated_storage(self):
        self.assertEqual(self.verify()['source_revision'], 'synthetic-source')
        self.assertTrue(self.stopped)
        self.assertEqual([call['mode'] for call in self.calls], [
            '--build-info', '--verify-bootstrap', '--probe-init', '--probe-disc',
            '--probe-es-metadata', '--video-hidden'])
        for call in self.calls[1:]:
            self.assertEqual(call['audio'], 'dummy')
            self.assertEqual(call['args'][call['args'].index('--volume') + 1], '0')
            self.assertTrue(Path(call['cwd']).name.startswith('native-verify-'))
            self.assertEqual(Path(call['cwd']).parent, self.root / 'setup/staging')

    def test_clean_shutdown_taking_more_than_five_seconds_passes(self):
        self.assertEqual(self.verify('slow-clean')['source_revision'], 'synthetic-source')
        self.assertTrue(self.stopped)
        self.assertIn('synthetic shutdown complete', self.log)

    def test_first_frame_does_not_hide_shutdown_crash(self):
        with self.assertRaisesRegex(ValueError, 'exited with code 1;'):
            self.verify('shutdown-failure')
        self.assertTrue(self.stopped)
        self.assertIn('Signal 11', self.log)

    def test_first_frame_does_not_accept_unhandled_sigterm(self):
        with self.assertRaisesRegex(ValueError, 'exited with code -15;'):
            self.verify('unhandled-signal')
        self.assertTrue(self.stopped)

    def test_markers_without_a_frame_row_fail(self):
        with self.assertRaisesRegex(ValueError, 'verified first frame'):
            self.verify('no-frame')
        self.assertFalse(self.stopped)

    def test_frame_row_without_every_startup_marker_fails(self):
        for index in range(3):
            with self.subTest(marker=index), self.assertRaisesRegex(ValueError, 'verified first frame'):
                self.verify(f'missing-marker-{index}')
            self.assertFalse(self.stopped)

    def test_failure_before_first_frame_fails(self):
        with self.assertRaisesRegex(ValueError, 'exited with code 19;'):
            self.verify('early-failure')
        self.assertFalse(self.stopped)

    def test_every_native_prerequisite_must_succeed_before_video(self):
        for mode in ('--verify-bootstrap', '--probe-init', '--probe-disc', '--probe-es-metadata'):
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, 'exited with code 17;'):
                self.verify(mode)
            self.assertEqual(self.calls[-1]['mode'], mode)
            self.assertNotIn('--video-hidden', [call['mode'] for call in self.calls])

    def test_identity_game_revision_and_hash_mismatches_stop_before_probes(self):
        for changes in (dict(source_revision='wrong-source'), dict(source_modified=True),
                        dict(source_sha256='b' * 64), dict(game_id='wrong-game'),
                        dict(game_revision=1), dict(dol_sha1='0' * 40)):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'provenance'):
                self.verify(build_changes=changes)
            self.assertEqual([call['mode'] for call in self.calls], ['--build-info'])
            self.assertEqual(self.events, [])


@unittest.skipUnless(os.environ.get('KIRBY_NATIVE_TEST'),'Set KIRBY_NATIVE_TEST for actual verifier checks')
class ActualNativeVerificationTests(unittest.TestCase):
    def test_actual_copied_program_passes_boot_sdk_and_a_hidden_first_frame(self):
        root=Path(__file__).resolve().parents[2]
        program=Path(os.environ['KIRBY_NATIVE_TEST']).resolve()
        record=tomllib.loads(subprocess.check_output([str(program),'--build-info'],text=True))['build']
        identity={key:record[key] for key in ('source_revision','source_modified','source_sha256')}
        with tempfile.TemporaryDirectory(prefix='kirby-verifier-') as directory:
            copy=Path(directory)/'WiiCompiled';shutil.copy2(program,copy)
            shutil.copy2(program.parent/'dsp_coef.bin',copy.parent/'dsp_coef.bin')
            events=[]
            with Job(Path(directory)/'setup','native-check',events.append) as job:
                build=verify_native(job,root,copy,root/'disc/DATA',identity)
            self.assertEqual(build['source_sha256'],identity['source_sha256'])
            self.assertEqual([event['check'] for event in events],['bootstrap','init','disc','es','video'])
            self.assertEqual(list((Path(directory)/'setup/staging').iterdir()),[])
