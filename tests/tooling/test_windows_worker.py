# SPDX-License-Identifier: GPL-3.0-only
"""Portable boundary tests plus native Win32 process/pipe integration checks."""
import json
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from build_layout import BuildLayout
from installer.installations import valid_resources, DSP_COEF_SHA256, InstallationStore
from installer.inputs import build_keys, host_compile_jobs
from installer.jobs import Cancelled, Job, run_process
from installer.platforms import executable_name, open_read
from installer.sources import _dolphin, archive_member
from installer.verification import check_pe


class WindowsBoundaryTests(unittest.TestCase):
    def test_pe_validation_rejects_other_architectures_dlls_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'WiiCompiled.exe'
            header = bytearray(64 + 264)
            header[:2] = b'MZ'; struct.pack_into('<I', header, 60, 64)
            header[64:68] = b'PE\0\0'
            struct.pack_into('<H', header, 68, 0x8664)
            struct.pack_into('<HHH', header, 84, 240, 2, 0x20b)
            output.write_bytes(header); check_pe(output)
            for offset, value in ((68, 0x14c), (68, 0xaa64), (86, 0x2002), (86, 0), (88, 0x10b)):
                altered = bytearray(header); struct.pack_into('<H', altered, offset, value)
                output.write_bytes(altered)
                with self.assertRaises(ValueError): check_pe(output)
            output.write_bytes(header[:-1])
            with self.assertRaises(ValueError): check_pe(output)

    def test_target_names_and_keys_are_separate(self):
        self.assertEqual(executable_name('windows'), 'WiiCompiled.exe')
        self.assertEqual(executable_name('linux'), 'WiiCompiled')
        self.assertEqual(executable_name('macos'), 'WiiCompiled')
        with self.assertRaises(ValueError): executable_name('unsupported')
        with tempfile.TemporaryDirectory() as directory:
            linux = build_keys(Path(directory), {}, 'a' * 64, target='linux')
            windows = build_keys(Path(directory), {}, 'a' * 64, target='windows')
            macos = build_keys(Path(directory), {}, 'a' * 64, target='macos')
            self.assertEqual(linux['translation'], windows['translation'])
            self.assertEqual(linux['translation'], macos['translation'])
            for key in ('cache', 'native'):
                self.assertEqual(len({linux[key], windows[key], macos[key]}), 3)

    def test_windows_resources_allow_only_hashed_dlls_and_required_dsp(self):
        dsp=dict(file='dsp_coef.bin',sha256=DSP_COEF_SHA256,size=4096)
        dll=dict(file='webgpu_dawn.dll',sha256='a'*64,size=20)
        with patch('installer.installations.WINDOWS',True):
            self.assertTrue(valid_resources([dsp,dll]))
            for entries in ([dll],[dsp,dll,dict(dll,file='WEBGPU_DAWN.DLL')],
                            [dsp,dict(dll,file='../bad.dll')],[dsp,dict(dll,file='NUL.dll')],
                            [dsp,dict(dll,file='bad.exe')],[dsp,dict(dll,size=True)],
                            [dsp,dict(dll,sha256='invalid')]):
                self.assertFalse(valid_resources(entries),entries)
        with patch('installer.installations.WINDOWS',False):
            self.assertFalse(valid_resources([dsp,dll]))

    def test_windows_installation_copies_and_revalidates_dll_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);candidate=root/'candidate';candidate.mkdir()
            exe=b'native PE fixture';dll=b'library';dsp=bytes(4096)
            (candidate/'WiiCompiled.exe').write_bytes(exe)
            (candidate/'dependency.dll').write_bytes(dll)
            (candidate/'dsp_coef.bin').write_bytes(dsp)
            dsp_hash=hashlib.sha256(dsp).hexdigest()
            manifest=dict(version=1,game_id='RK5E01',revision=0,dol_sha1='f4280a2b0709bfde4132be22e9f7fa8ecfa2a130',
                          data_root=str(root/'DATA'),project_sha256='1'*64,translation_key='2'*64,toolchain_image_id='3'*64,
                          executable=dict(file='WiiCompiled.exe',sha256=hashlib.sha256(exe).hexdigest(),size=len(exe)),
                          verification=['bootstrap','init','disc','es','video'],
                          resources=[dict(file='dsp_coef.bin',sha256=dsp_hash,size=4096),
                                     dict(file='dependency.dll',sha256=hashlib.sha256(dll).hexdigest(),size=len(dll))])
            with patch('installer.installations.WINDOWS',True), patch('installer.installations.executable_name',return_value='WiiCompiled.exe'), patch('installer.installations.DSP_COEF_SHA256',dsp_hash):
                store=InstallationStore(root);identifier=store.publish(candidate,manifest)
                store.activate(identifier)
                installed=store.executable(identifier).parent/'dependency.dll'
                self.assertEqual(installed.read_bytes(),dll)
                installed.write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError,'resource digest'): store.manifest(identifier)

    def test_windows_memory_does_not_read_proc(self):
        with patch('installer.inputs.WINDOWS', True), patch('installer.inputs.windows_available_memory', return_value=6 * 1024**3), patch('installer.inputs.os.cpu_count', return_value=8):
            self.assertEqual(host_compile_jobs(), 3)

    def test_windows_extractor_is_native_dolphin_tool(self):
        # Exercise PATH fallback independently of developer tool caches.
        for adapter in ('WindowsTools', 'PortableTools'):
            self.enterContext(patch(f'installer.sources.{adapter}.find', return_value=None))
        with patch('installer.sources.WINDOWS', True), patch('installer.sources.shutil.which', side_effect=lambda name: 'D:/Dolphin/DolphinTool.exe' if name == 'DolphinTool.exe' else None):
            self.assertEqual(_dolphin('source', 'output'), ['D:/Dolphin/DolphinTool.exe'])
        with patch('installer.sources.WINDOWS', True), patch('installer.sources.shutil.which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'DolphinTool.exe'): _dolphin('source', 'output')

    def test_archive_rejects_windows_drive_and_stream_names(self):
        for member in ('C:/escape.wbfs', 'safe:stream.wbfs'):
            with self.assertRaises(ValueError): archive_member(f'Path = {member}\nSize = 100\n')

    def test_layout_uses_shared_token_and_migrates_only_matching_legacy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); stage = root / 'local/setup/builds/job'
            stage.mkdir(parents=True)
            info = root.stat()
            legacy = dict(version=1, project_identity=dict(device=info.st_dev, inode=info.st_ino), kind='kirby-native-build-stage')
            marker = stage / '.layout.json'; marker.write_text(json.dumps(legacy))
            BuildLayout(root, stage).ensure()
            record = json.loads(marker.read_text())
            self.assertEqual(record['version'], 2)
            self.assertEqual(record['project_identity']['token'], (root / 'local/setup/project-id').read_text().strip())
            BuildLayout(root, stage).ensure()
            legacy['project_identity']['inode'] += 1
            marker.write_text(json.dumps(legacy))
            with self.assertRaisesRegex(ValueError, 'another'): BuildLayout(root, stage).ensure()


@unittest.skipUnless(os.name == 'nt', 'Requires real Win32 pipes, locks and Job Objects')
class WindowsIntegrationTests(unittest.TestCase):
    def test_layout_accepts_short_checkout_path(self):
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetShortPathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
        kernel.GetShortPathNameW.restype = wintypes.DWORD
        with tempfile.TemporaryDirectory(prefix='kirby long checkout name ') as directory:
            buffer = ctypes.create_unicode_buffer(32768)
            length = kernel.GetShortPathNameW(directory, buffer, len(buffer))
            if not length or length >= len(buffer):
                raise ctypes.WinError(ctypes.get_last_error())
            root = Path(buffer.value)
            if root == Path(directory).resolve():
                self.skipTest('This volume does not provide 8.3 short path aliases')
            for exists in (False, True):
                stage = root / 'local/setup/builds' / ('existing' if exists else 'new')
                if exists:
                    stage.mkdir(parents=True)
                layout = BuildLayout(root, stage)
                layout.ensure()
                self.assertEqual(layout.stage, stage.resolve())
                BuildLayout(Path(directory).resolve(), stage).ensure()

    def test_native_lock_pipe_output_and_cancellation(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / 'setup'
            with Job(workspace, 'first', lambda event: None) as job:
                with self.assertRaisesRegex(ValueError, 'running'):
                    with Job(workspace, 'second', lambda event: None): pass
                output = run_process(job, [sys.executable, '-c', "print('native output')"], capture_limit=100)
                self.assertEqual(output.replace(b'\r\n', b'\n'), b'native output\n')
                timer = threading.Timer(0.3, job.cancel.set); timer.start()
                try:
                    with self.assertRaises(Cancelled):
                        run_process(job, [sys.executable, '-c', 'import time; time.sleep(30)'])
                finally:
                    timer.join()
            with Job(workspace,'after-release',lambda event:None):
                pass  # Closing the previous job handle releases the byte lock.

    def test_cancellation_stops_descendants_even_with_open_inherited_stdout(self):
        import ctypes
        from ctypes import wintypes
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);marker=root/'child.pid'
            script=("import subprocess,sys,time,pathlib; "
                    "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)']); "
                    f"pathlib.Path({str(marker)!r}).write_text(str(child.pid)); time.sleep(30)")
            with Job(root/'setup','tree',lambda event:None) as job:
                def cancel_after_spawn():
                    deadline=time.monotonic()+10
                    while not marker.exists() and time.monotonic()<deadline:
                        time.sleep(0.01)
                    job.cancel.set()
                thread=threading.Thread(target=cancel_after_spawn);thread.start()
                try:
                    with self.assertRaises(Cancelled):
                        run_process(job,[sys.executable,'-c',script],timeout=15)
                finally:
                    thread.join()
            self.assertTrue(marker.is_file(),'Child failed to spawn before cancellation')
            kernel=ctypes.WinDLL('kernel32',use_last_error=True)
            kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
            kernel.OpenProcess.restype=wintypes.HANDLE
            kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
            kernel.CloseHandle.argtypes=[wintypes.HANDLE]
            handle=kernel.OpenProcess(0x100000,False,int(marker.read_text()))
            if handle:
                try:self.assertEqual(kernel.WaitForSingleObject(handle,1000),0,'Child survived cancellation')
                finally:kernel.CloseHandle(handle)

    def test_worker_exits_while_launcher_keeps_stdin_pipe_open(self):
        root=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            process=subprocess.Popen([sys.executable,str(root/'tools/install.py'),'--workspace',str(Path(directory)/'setup')],
                                     stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                process.stdin.write(json.dumps(dict(id='missing',operation='inspect',source=str(Path(directory)/'missing.wbfs')))+'\n')
                process.stdin.flush();process.wait(timeout=10)
                errors=process.stderr.read()
                self.assertEqual(process.returncode,1,errors)
                events=[json.loads(line) for line in process.stdout.read().splitlines()]
                self.assertTrue(events,f'Worker exited without protocol output. stderr: {errors}')
                self.assertEqual(errors,'')
                self.assertEqual(events[-1]['state'],'failed')
            finally:
                if process.poll() is None:process.kill();process.wait()
                process.stdin.close();process.stdout.close();process.stderr.close()

    def test_windows_safe_reads_reject_junctions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); real = root / 'real'; real.mkdir()
            (real / 'file').write_bytes(b'safe')
            junction = root / 'junction'
            result=subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(junction), str(real)], capture_output=True,text=True)
            if not junction.exists():
                import ctypes
                wine=hasattr(ctypes.WinDLL('ntdll'),'wine_get_version')
                if wine:
                    self.skipTest('Wine mklink /J created no junction; junction safety still requires native Windows/NTFS validation')
                self.fail(f'mklink /J failed to create a junction (exit {result.returncode}): {result.stdout} {result.stderr}')
            self.assertEqual(result.returncode,0,result.stderr)
            try:
                with self.assertRaises(ValueError): open_read(junction / 'file')
                # Resolving a stage must not hide a junction in the supplied
                # stage path or one of its ancestors.
                base = root / 'local/setup/builds'
                base.mkdir(parents=True)
                stage_link = base / 'linked'
                result = subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(stage_link), str(real)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                try:
                    with self.assertRaisesRegex(ValueError, 'symlinks'):
                        BuildLayout(root, stage_link).ensure()
                    with self.assertRaisesRegex(ValueError, 'symlinks'):
                        BuildLayout(root, stage_link / 'child').ensure()
                    self.assertEqual(list(real.iterdir()), [real / 'file'])
                finally:
                    stage_link.rmdir()
            finally:
                junction.rmdir()
