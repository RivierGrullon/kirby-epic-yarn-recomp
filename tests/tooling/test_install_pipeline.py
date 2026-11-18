# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import kirby
import install as worker
from build_layout import BuildLayout
from installer.installations import InstallationStore
import installer.installations as installations
from installer.jobs import Cancelled, Job
from installer.pipeline import execute_install, BuildTools
from installer.container import ContainerResult
import installer.pipeline as pipeline
import stat
from tooling.test_prepare import fixture


class Tools:
    local = None
    build_command = BuildTools.build_command
    def __init__(self,root):
        self.root=root;self.fail=None
        self.record=dict(source_revision='a'*40,source_modified=False,source_sha256='b'*64)
    def bootstrap(self):pass
    def image(self):return 'c'*64
    def identity(self):return dict(self.record)
    def jobs(self):return 1
    def run_tool(self,arguments,**options):
        command='compile' if 'tools/build_native.py' in arguments else 'translate'
        if self.fail==command:raise ValueError('Synthetic '+command+' failed')
        stage=self.root/arguments[arguments.index('--stage-root')+1]
        layout=BuildLayout(self.root,stage);layout.ensure()
        if command=='translate':
            (layout.generated/'function.cpp').write_text('synthetic translated output')
        else:
            if '--game-only' in arguments:
                raise AssertionError('The install worker must also build the launcher')
            layout.build.mkdir(exist_ok=True)
            (layout.build/('kirby-launcher.exe' if pipeline.WINDOWS else 'kirby-launcher')).write_bytes(b'launcher')
            (layout.build/'WiiCompiled').write_bytes(b'synthetic native output')
            (layout.build/'WiiCompiled').chmod(0o700)
            (layout.build/'dsp_coef.bin').write_bytes(bytes(4096))
    def verify(self,executable,data,identity):
        if self.fail=='verify':raise ValueError('Synthetic verification failed')
        return dict(identity,compiler='fixture compiler',build_type='Release')


class InstallPipelineTests(unittest.TestCase):
    def test_compiler_parallelism_is_capped_by_container_machine_memory(self):
        from installer.pipeline import BuildTools
        from types import SimpleNamespace
        tools=BuildTools(None,Path('/fixture'))
        tools.image_id='a'*64
        with patch('installer.pipeline.host_compile_jobs',return_value=4),patch('installer.pipeline.run_container',return_value=SimpleNamespace(output=b'1\n')):
            self.assertEqual(tools.jobs(),1)

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='kirby-pipeline-');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.data=self.root/'selected/DATA';(self.data/'sys').mkdir(parents=True)
        dol=fixture();(self.data/'sys/main.dol').write_bytes(dol)
        self.sha=hashlib.sha1(dol).hexdigest()
        for module,name,value in ((kirby,'EXPECTED_SHA1',self.sha),(installations,'EXPECTED_SHA1',self.sha),
                                  (installations,'DSP_COEF_SHA256',hashlib.sha256(bytes(4096)).hexdigest())):
            change=patch.object(module,name,value);change.start();self.addCleanup(change.stop)
        self.source=dict(data_root=str(self.data),source=str(self.data),source_kind='directory',dol_sha1=self.sha,
                         game_id='RK5E01',revision=0,asset_files=1,asset_bytes=4)
        verify_launcher = patch('installer.launcher.run_process', side_effect=lambda *a,**kw: (
            'schema_version=1\nprogram="launcher"\n[build]\nsource_revision="'+self.tools.record['source_revision']+
            '"\nsource_sha256="'+self.tools.record['source_sha256']+'"\nsource_modified=false\n').encode())
        verify_launcher.start();self.addCleanup(verify_launcher.stop)
        self.tools=Tools(self.root)
        self.store=InstallationStore(self.root)
        self.events=[]
        self.saved=self.root/'local/user/settings.toml';self.saved.parent.mkdir(parents=True);self.saved.write_bytes(b'keep settings')
    def job(self,identifier):return Job(self.root/'local/setup',identifier,self.events.append)

    def test_success_selects_an_independent_verified_build_and_reuses_matching_cache(self):
        with self.job('first') as job:
            result=execute_install(job,self.root,self.source,services=self.tools)
        identifier=result['installation_id']
        self.assertEqual(result['source'],str(self.data))
        self.assertEqual(self.store.read_active()['current'],identifier)
        self.assertEqual(self.store.executable(identifier).read_bytes(),b'synthetic native output')
        self.assertEqual(self.saved.read_bytes(),b'keep settings')
        (self.root/'build/native/kirby-launcher').unlink()
        self.tools.fail='compile'
        with self.job('second') as job:
            repeated=execute_install(job,self.root,self.source,services=self.tools)
        self.assertEqual(repeated['installation_id'],identifier)
        self.assertTrue(repeated['build_reused'])
        self.assertEqual((self.root/'build/native/kirby-launcher').read_bytes(),b'launcher')

    def test_failed_rebuild_keeps_previous_selection_and_configuration(self):
        with self.job('first') as job:execute_install(job,self.root,self.source,services=self.tools)
        previous=self.store.read_active()
        self.tools.record['source_sha256']='d'*64;self.tools.fail='compile'
        with self.job('failed') as job,self.assertRaisesRegex(ValueError,'compile'):
            execute_install(job,self.root,self.source,services=self.tools)
        self.assertEqual(self.store.read_active(),previous)
        self.assertEqual(self.saved.read_bytes(),b'keep settings')

    def test_missing_cached_launcher_rebuilds_without_retranslation(self):
        with self.job('first') as job:
            execute_install(job,self.root,self.source,services=self.tools)
        next((self.root/'local/setup/builds').glob('cache-*/build/kirby-launcher')).unlink()
        with self.job('repair') as job:
            result=execute_install(job,self.root,self.source,services=self.tools)
        self.assertTrue(result['translation_reused'])
        self.assertFalse(result['build_reused'])
        self.assertTrue((self.root/'build/native/kirby-launcher').is_file())

    def test_native_verification_failure_never_selects_an_executable(self):
        self.tools.fail='verify'
        with self.job('failed') as job,self.assertRaisesRegex(ValueError,'verification'):
            execute_install(job,self.root,self.source,services=self.tools)
        self.assertIsNone(self.store.read_active())

    def test_launcher_preparation_failure_preserves_previous_selection(self):
        with self.job('first') as job:
            execute_install(job,self.root,self.source,services=self.tools)
        previous=self.store.read_active()
        self.tools.record['source_sha256']='d'*64
        def fail_launcher(job,root,build,identity):
            self.assertTrue((build/'WiiCompiled').is_file())
            self.assertEqual(identity['source_sha256'],'d'*64)
            raise ValueError('Launcher unavailable')
        with patch.object(pipeline,'install_launcher',side_effect=fail_launcher), self.job('launcher-failed') as job,self.assertRaisesRegex(ValueError,'Launcher unavailable'):
            execute_install(job,self.root,self.source,services=self.tools)
        self.assertEqual(self.store.read_active(),previous)

    def test_cancel_before_selection_does_not_change_previous_build(self):
        with self.job('first') as job:execute_install(job,self.root,self.source,services=self.tools)
        previous=self.store.read_active();self.tools.record['source_sha256']='d'*64
        with self.job('cancelled') as job:
            real_verify=self.tools.verify
            def cancel(executable,data,identity):
                result=real_verify(executable,data,identity);job.cancel.set();return result
            with patch.object(self.tools,'verify',side_effect=cancel),self.assertRaises(Cancelled):
                execute_install(job,self.root,self.source,services=self.tools)
        self.assertEqual(self.store.read_active(),previous)

    def test_changed_dol_fails_before_translation_or_selection(self):
        (self.data/'sys/main.dol').write_bytes(b'changed after inspection')
        with self.job('changed') as job,self.assertRaisesRegex(ValueError,'DOL changed'):
            execute_install(job,self.root,self.source,services=self.tools)
        self.assertIsNone(self.store.read_active())

    def test_source_changes_during_build_do_not_publish_a_mixed_build(self):
        original=self.tools.run_tool
        def change(arguments,**options):
            result=original(arguments)
            if 'tools/build_native.py' in arguments:self.tools.record['source_sha256']='e'*64
            return result
        with self.job('changed') as job,patch.object(self.tools,'run_tool',side_effect=change):
            with self.assertRaisesRegex(ValueError,'sources changed'):
                execute_install(job,self.root,self.source,services=self.tools)
        self.assertIsNone(self.store.read_active())

    def test_corrupt_cached_output_is_rebuilt_instead_of_selected(self):
        with self.job('first') as job:execute_install(job,self.root,self.source,services=self.tools)
        executable=next((self.root/'local/setup/builds').glob('cache-*/build/WiiCompiled'))
        executable.write_bytes(b'corrupted cache output')
        with self.job('repair') as job:
            result=execute_install(job,self.root,self.source,services=self.tools)
        self.assertFalse(result['build_reused'])
        self.assertEqual(self.store.executable(result['installation_id']).read_bytes(),b'synthetic native output')

    def test_cancel_after_selection_is_reported_as_committed(self):
        original=self.store.activate
        def late_cancel(identifier,**options):
            value=original(identifier,**options);job.cancel.set();return value
        with self.job('commit') as job,patch.object(InstallationStore,'activate',side_effect=late_cancel):
            result=execute_install(job,self.root,self.source,services=self.tools)
            job.check_cancel()
            self.assertTrue(result['committed'])

    def test_windows_candidate_keeps_dlls_and_writable_temporary_files(self):
        original=self.tools.run_tool
        def build(arguments,**options):
            result=original(arguments)
            if 'tools/build_native.py' in arguments:
                self.assertEqual(arguments[arguments.index('--target')+1],'windows')
                stage=self.root/arguments[arguments.index('--stage-root')+1]
                output=stage/'build'
                (output/'WiiCompiled').rename(output/'WiiCompiled.exe')
                (output/'dependency.DLL').write_bytes(b'Windows runtime dependency')
            return result
        def verify(executable,data,identity):
            self.assertEqual(executable.name,'WiiCompiled.exe')
            self.assertTrue(executable.stat().st_mode & stat.S_IWUSR)
            self.assertEqual((executable.parent/'dependency.DLL').read_bytes(),b'Windows runtime dependency')
            return dict(identity)
        with (patch.object(self.tools,'run_tool',side_effect=build),patch.object(self.tools,'verify',side_effect=verify),
              patch('installer.launcher.WINDOWS',True),patch.object(pipeline,'WINDOWS',True),patch.object(pipeline,'executable_name',return_value='WiiCompiled.exe'),
              patch.object(installations,'WINDOWS',True),patch.object(installations,'executable_name',return_value='WiiCompiled.exe')):
            with self.job('windows') as job:
                result=execute_install(job,self.root,self.source,services=self.tools)
            manifest=self.store.manifest(result['installation_id'])
            self.assertEqual(manifest['executable']['file'],'WiiCompiled.exe')
            self.assertEqual({resource['file'] for resource in manifest['resources']},{'dsp_coef.bin','dependency.DLL'})
        self.assertEqual(list((self.root/'local/setup/staging').iterdir()),[])

    def test_windows_provenance_uses_same_container_filesystem_view_as_compiler(self):
        tools=BuildTools(None,self.root);tools.image_id='c'*64
        record=self.tools.identity()
        with (patch.object(pipeline,'WINDOWS',True),
              patch.object(pipeline,'run_container',return_value=ContainerResult('c'*64,json.dumps(record).encode())) as run,
              patch.object(pipeline.build_info,'collect',side_effect=AssertionError('Host filesystem modes differ'))):
            self.assertEqual(tools.identity(),record)
        self.assertEqual(run.call_args.kwargs['image_id'],'c'*64)
        self.assertIn('build_info.collect',run.call_args.args[2][2])

    def test_worker_routes_install_and_emits_selection_only_after_success(self):
        output=io.StringIO()
        with tempfile.TemporaryFile(mode='w+') as request:
            request.write(json.dumps(dict(version=1,id='worker-install',operation='install',source=str(self.data)))+'\n')
            request.seek(0)
            def execute(job,root,source):
                return execute_install(job,root,source,services=self.tools)
            with (patch.object(sys,'argv',['install.py','--workspace',str(self.root/'local/setup')]),
                  patch.object(sys,'stdin',request),patch.object(worker,'ROOT',self.root),
                  patch.object(worker.signal,'signal'),patch.object(worker,'inspect_source',return_value=self.source),
                  patch.object(worker,'execute_install',side_effect=execute),contextlib.redirect_stdout(output)):
                self.assertEqual(worker.main(),0)
        events=[json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([event['sequence'] for event in events],list(range(1,len(events)+1)))
        self.assertEqual((events[-1]['stage'],events[-1]['state']),('install','complete'))
        self.assertEqual(events[-1]['result']['installation_id'],self.store.read_active()['current'])
