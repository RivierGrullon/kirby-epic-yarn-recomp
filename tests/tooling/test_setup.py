# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from installer import launcher

spec=importlib.util.spec_from_file_location('kirby_setup_command',ROOT/'tools/setup.py')
setup=importlib.util.module_from_spec(spec);spec.loader.exec_module(setup)


class SetupCommandTests(unittest.TestCase):
    def test_help_is_available_without_dependencies(self):
        result = subprocess.run([sys.executable, ROOT/'tools/setup.py', '--help'],
                                capture_output=True, text=True, env=dict(os.environ, PATH=''))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--check', result.stdout)

    def test_missing_dependencies_are_explained_before_building(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'my own DATA'; source.mkdir()
            result = subprocess.run([sys.executable, ROOT/'tools/setup.py', '--check', source],
                                    capture_output=True, text=True, env=dict(os.environ, PATH=''))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('podman', result.stderr)
            self.assertNotIn('Traceback', result.stderr)
            self.assertNotIn('dolphin-tool', result.stderr)
            self.assertEqual(list(source.iterdir()), [])

    def test_missing_source_explains_example_paths(self):
        result = subprocess.run([sys.executable, ROOT/'tools/setup.py', '/path/to/your-game.7z'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('actual path', result.stderr)
        self.assertNotIn('Traceback', result.stderr)

    @unittest.skipIf(os.name == 'nt', 'Bash compatibility wrapper is Linux-only')
    def test_container_wrapper_explains_missing_podman(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'dirname').symlink_to(shutil.which('dirname'))
            result = subprocess.run([shutil.which('bash'), ROOT/'tools/container.sh', '--build'],
                                    capture_output=True, text=True, env={'PATH': directory})
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('docs/building.md', result.stderr)
            self.assertNotIn('exec: podman: not found', result.stderr)


class SetupPreflightTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.source=Path(self.temp.name)/'DATA';self.source.mkdir()

    def preflight(self,engine,output='linux',returncode=0):
        with (patch.object(setup.sys,'platform','linux'),patch.object(setup,'WINDOWS',False),
              patch.object(setup.platform,'machine',return_value='x86_64'),
              patch('container_engine.select_engine',return_value=engine),
              patch.object(setup.shutil,'which',return_value='/fixture/tool'),
              patch('installer.inputs.host_compile_jobs',return_value=2),
              patch.object(setup.subprocess,'run',return_value=SimpleNamespace(returncode=returncode,stdout=output,stderr='daemon offline')) as run):
            result=setup.preflight(self.source,engine)
            return result,run.call_args.args[0]

    def test_each_engine_queries_its_actual_server_os_field(self):
        for engine,field in (('podman','{{.Host.OS}}'),('docker','{{.OSType}}')):
            result,command=self.preflight(engine)
            self.assertEqual(result,engine)
            self.assertEqual(command,[engine,'info','--format',field])

    def test_native_windows_still_requires_linux_container_backend(self):
        with self.assertRaisesRegex(ValueError,'Linux containers'):
            self.preflight('docker','windows')
        with self.assertRaisesRegex(ValueError,'installed but unavailable'):
            self.preflight('podman',returncode=125)

    def test_old_python_has_actionable_error_before_other_imports(self):
        with patch.object(setup.sys,'version_info',(3,10)),patch('container_engine.select_engine') as select:
            with self.assertRaisesRegex(ValueError,'Python 3.11'):
                setup.preflight(self.source)
            select.assert_not_called()

    def test_windows_requires_the_same_native_extractor_used_by_worker(self):
        image=self.source.parent/'game.wbfs';image.write_bytes(b'fixture')
        with (patch.object(setup,'WINDOWS',True),patch.object(setup.sys,'platform','win32'),
              patch.object(setup.platform,'machine',return_value='AMD64'),
              patch('container_engine.select_engine',return_value='docker'),
              patch.object(setup.shutil,'which',side_effect=lambda name:'/fixture/tool' if name in ('git','dolphin-tool','flatpak') else None),
              patch.object(setup.subprocess,'run') as run):
            with self.assertRaisesRegex(ValueError,'DolphinTool.exe'):
                setup.preflight(image)
            run.assert_not_called()

    def test_check_does_not_create_setup_jobs_or_compile(self):
        output=io.StringIO()
        with (patch.object(setup.sys,'argv',['setup.py','--check',str(self.source)]),
              patch.object(setup,'preflight',return_value='docker'),patch('installer.jobs.Job') as job,
              patch.dict(os.environ,{},clear=False),contextlib.redirect_stdout(output)):
            self.assertEqual(setup.main(),0)
            job.assert_not_called()
        self.assertIn('Container engine: docker',output.getvalue())

    def test_windows_missing_graphics_runtime_is_reported_before_compilation(self):
        with (patch.object(setup,'WINDOWS',True),patch.object(setup.sys,'platform','win32'),
              patch.object(setup.platform,'machine',return_value='AMD64'),
              patch('container_engine.select_engine',return_value='docker'),
              patch.object(setup.shutil,'which',return_value='/fixture/tool'),
              patch.object(setup,'windows_runtime_missing',return_value=['MSVCP140.dll']),
              patch.object(setup.subprocess,'run') as run):
            with self.assertRaisesRegex(ValueError,'Visual C\\+\\+'):
                setup.preflight(self.source)
            run.assert_not_called()


class LauncherPublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.staging=self.root/'staging';self.staging.mkdir()
        self.destination=self.root/'native';self.destination.mkdir()
        self.job=Mock();self.job.check_cancel=Mock()

    def test_updated_libraries_and_launcher_replace_previous_products(self):
        for name in ('runtime.dll','kirby-launcher.exe'):
            (self.staging/name).write_bytes(b'new '+name.encode())
            (self.destination/name).write_bytes(b'old '+name.encode())
        launcher._publish_launcher(self.staging,self.destination,['runtime.dll','kirby-launcher.exe'],self.job)
        self.assertEqual((self.destination/'runtime.dll').read_bytes(),b'new runtime.dll')
        self.assertEqual((self.destination/'kirby-launcher.exe').read_bytes(),b'new kirby-launcher.exe')

    def test_failed_executable_replacement_restores_dll_and_removes_new_files(self):
        for name in ('runtime.dll','added.dll','kirby-launcher.exe'):
            (self.staging/name).write_bytes(b'new')
        (self.destination/'runtime.dll').write_bytes(b'old runtime')
        (self.destination/'kirby-launcher.exe').write_bytes(b'old launcher')
        replace=os.replace
        def locked(source,target):
            if Path(source)==self.staging/'kirby-launcher.exe':
                raise PermissionError('launcher is running')
            return replace(source,target)
        with patch.object(setup.os,'replace',side_effect=locked):
            with self.assertRaisesRegex(PermissionError,'running'):
                launcher._publish_launcher(self.staging,self.destination,['runtime.dll','added.dll','kirby-launcher.exe'],self.job)
        self.assertEqual((self.destination/'runtime.dll').read_bytes(),b'old runtime')
        self.assertEqual((self.destination/'kirby-launcher.exe').read_bytes(),b'old launcher')
        self.assertFalse((self.destination/'added.dll').exists())

    def test_unrecoverable_rollback_preserves_previous_bytes_for_recovery(self):
        for name in ('runtime.dll','kirby-launcher.exe'):
            (self.staging/name).write_bytes(b'new')
            (self.destination/name).write_bytes(b'old')
        replace=os.replace
        def locked(source,target):
            if Path(source)==self.staging/'kirby-launcher.exe' or Path(source).parent.name=='previous':
                raise PermissionError('file is locked')
            return replace(source,target)
        with patch.object(setup.os,'replace',side_effect=locked):
            with self.assertRaisesRegex(ValueError,'could not restore'):
                launcher._publish_launcher(self.staging,self.destination,['runtime.dll','kirby-launcher.exe'],self.job)
        recovery=list(self.destination.glob('.launcher-recovery-*'))
        self.assertEqual(len(recovery),1)
        self.assertEqual((recovery[0]/'runtime.dll').read_bytes(),b'old')

    def test_cancel_before_publication_leaves_current_launcher_and_dependencies(self):
        from installer.jobs import Cancelled
        for name in ('runtime.dll','kirby-launcher.exe'):
            (self.staging/name).write_bytes(b'new')
            (self.destination/name).write_bytes(b'old')
        self.job.check_cancel.side_effect=Cancelled('cancelled')
        with self.assertRaises(Cancelled):
            launcher._publish_launcher(self.staging,self.destination,['runtime.dll','kirby-launcher.exe'],self.job)
        self.assertEqual((self.destination/'runtime.dll').read_bytes(),b'old')
        self.assertEqual((self.destination/'kirby-launcher.exe').read_bytes(),b'old')

    def test_launcher_verifies_with_all_staged_dlls_before_replacing_old_copy(self):
        build=self.root/'local/setup/builds/job/build';build.mkdir(parents=True)
        (build/'kirby-launcher.exe').write_bytes(b'new launcher')
        (build/'runtime.dll').write_bytes(b'new library')
        destination=self.root/'build/native';destination.mkdir(parents=True)
        (destination/'kirby-launcher.exe').write_bytes(b'old launcher')
        (destination/'runtime.dll').write_bytes(b'old library')
        identity=dict(source_revision='a'*40,source_modified=False,source_sha256='b'*64)
        def verify(job,command,**kwargs):
            candidate=Path(command[0])
            self.assertEqual((candidate.parent/'runtime.dll').read_bytes(),b'new library')
            self.assertEqual((destination/'runtime.dll').read_bytes(),b'old library')
            return ('schema_version=1\nprogram="launcher"\n[build]\nsource_revision="'+'a'*40+
                    '"\nsource_modified=false\nsource_sha256="'+'b'*64+'"\n').encode()
        with patch.object(launcher,'WINDOWS',True), patch.object(launcher,'run_process',side_effect=verify):
            result=launcher.install_launcher(self.job,self.root,build,identity)
        self.assertEqual(result,destination/'kirby-launcher.exe')
        self.assertEqual(result.read_bytes(),b'new launcher')
        self.assertEqual((destination/'runtime.dll').read_bytes(),b'new library')


class EnginePreferenceTests(unittest.TestCase):
    def test_explicit_backend_wins_and_saved_backend_is_reused(self):
        import container_engine
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'local/setup').mkdir(parents=True)
            (root/'local/setup/container-engine').write_text('docker\n')
            with patch.object(container_engine,'ROOT',root),patch.dict(os.environ,{},clear=True),patch.object(container_engine.shutil,'which',side_effect=lambda name:'/bin/'+name):
                self.assertEqual(container_engine.select_engine(),'docker')
                self.assertEqual(container_engine.select_engine('podman'),'podman')
                with patch.dict(os.environ,KIRBY_CONTAINER_ENGINE='podman'):
                    self.assertEqual(container_engine.select_engine('auto'),'podman')


if __name__ == '__main__':
    unittest.main()
