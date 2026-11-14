# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.verification import check_elf,verify_native
from installer.jobs import Job


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


@unittest.skipUnless(os.environ.get('KIRBY_NATIVE_TEST'),'Set KIRBY_NATIVE_TEST for actual verifier checks')
class ActualNativeVerificationTests(unittest.TestCase):
    def test_actual_copied_program_passes_boot_sdk_and_a_hidden_first_frame(self):
        import shutil
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
