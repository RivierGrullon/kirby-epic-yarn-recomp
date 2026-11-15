# SPDX-License-Identifier: GPL-3.0-only
"""Compile synthetic translated data into an actual Windows COFF object."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get('KIRBY_MINGW_TEST') and shutil.which('cmake'),
                     'Set KIRBY_MINGW_TEST to a native-hosted LLVM-MinGW directory')
class WindowsDataTests(unittest.TestCase):
    def test_linux_generated_payload_is_preserved_in_readonly_coff_section(self):
        with tempfile.TemporaryDirectory(prefix='kirby-coff-') as directory:
            root=Path(directory)
            payload=root/'payload.bin';payload.write_bytes(bytes([0x11,0x22,0x33,0x44]))
            source=root/'data.S'
            content=('.section .rodata,"a",@progbits\n.p2align 4\n.globl data_fixture\n'
                     f'data_fixture:\n.incbin "{payload.as_posix()}"\n.section .note.GNU-stack,"",@progbits\n')
            source.write_text(content)
            (root/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(CoffFixture LANGUAGES C ASM)
add_library(data OBJECT "${{CMAKE_CURRENT_SOURCE_DIR}}/data.S")
include("{(ROOT/'cmake/windows-data.cmake').as_posix()}")
kirby_use_windows_data_blob(data "${{CMAKE_CURRENT_SOURCE_DIR}}/data.S")
''')
            toolchain=Path(os.environ['KIRBY_MINGW_TEST']).resolve()
            command=['cmake','-S',str(root),'-B',str(root/'build'),'-G','Ninja',
                     '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/windows-toolchain.cmake'),
                     '-DKIRBY_LLVM_MINGW_ROOT='+str(toolchain)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run(['cmake','--build',str(root/'build')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            objects=list((root/'build').rglob('*.obj'))
            self.assertEqual(len(objects),1)
            dump=subprocess.check_output([toolchain/'bin/llvm-readobj','--file-headers','--sections','--section-data',objects[0]],text=True)
            self.assertIn('IMAGE_FILE_MACHINE_AMD64',dump)
            self.assertIn('Name: .rdata',dump)
            section=dump.split('Name: .rdata',1)[1].split('Section {',1)[0]
            self.assertIn('IMAGE_SCN_MEM_READ',section)
            self.assertNotIn('IMAGE_SCN_MEM_WRITE',section)
            self.assertIn('11223344',section.replace(' ',''))
            self.assertEqual(source.read_text(),content)
            self.assertEqual(payload.read_bytes(),bytes([0x11,0x22,0x33,0x44]))
            payload.write_bytes(bytes([0x55,0x66,0x77,0x88]))
            source.write_text(content+'// Updated payload\n')
            result=subprocess.run(['cmake','--build',str(root/'build')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            updated=subprocess.check_output([toolchain/'bin/llvm-readobj','--sections','--section-data',objects[0]],text=True)
            self.assertIn('55667788',updated.replace(' ',''))
