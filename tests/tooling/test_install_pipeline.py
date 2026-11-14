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
from installer.pipeline import execute_install
from tooling.test_prepare import fixture


class Tools:
    def __init__(self,root):
        self.root=root;self.fail=None
        self.record=dict(source_revision='a'*40,source_modified=False,source_sha256='b'*64)
    def bootstrap(self):pass
    def image(self):return 'c'*64
    def identity(self):return dict(self.record)
    def jobs(self):return 1
    def run(self,arguments):
        command='compile' if 'tools/build_native.py' in arguments else 'translate'
        if self.fail==command:raise ValueError('Synthetic '+command+' failed')
        stage=self.root/arguments[arguments.index('--stage-root')+1]
        layout=BuildLayout(self.root,stage);layout.ensure()
        if command=='translate':
            (layout.generated/'function.cpp').write_text('synthetic translated output')
        else:
            layout.build.mkdir(exist_ok=True)
            (layout.build/'WiiCompiled').write_bytes(b'synthetic native output')
            (layout.build/'WiiCompiled').chmod(0o700)
            (layout.build/'dsp_coef.bin').write_bytes(bytes(4096))
    def verify(self,executable,data,identity):
        if self.fail=='verify':raise ValueError('Synthetic verification failed')
        return dict(identity,compiler='fixture compiler',build_type='Release')


class InstallPipelineTests(unittest.TestCase):
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
        self.tools=Tools(self.root)
        self.store=InstallationStore(self.root)
        self.events=[]
        self.saved=self.root/'local/user/settings.toml';self.saved.parent.mkdir(parents=True);self.saved.write_bytes(b'keep settings')
    def job(self,identifier):return Job(self.root/'local/setup',identifier,self.events.append)

    def test_success_selects_an_independent_verified_build_and_reuses_matching_cache(self):
        with self.job('first') as job:
            result=execute_install(job,self.root,self.source,services=self.tools)
        identifier=result['installation_id']
        self.assertEqual(self.store.read_active()['current'],identifier)
        self.assertEqual(self.store.executable(identifier).read_bytes(),b'synthetic native output')
        self.assertEqual(self.saved.read_bytes(),b'keep settings')
        self.tools.fail='compile'
        with self.job('second') as job:
            repeated=execute_install(job,self.root,self.source,services=self.tools)
        self.assertEqual(repeated['installation_id'],identifier)
        self.assertTrue(repeated['build_reused'])

    def test_failed_rebuild_keeps_previous_selection_and_configuration(self):
        with self.job('first') as job:execute_install(job,self.root,self.source,services=self.tools)
        previous=self.store.read_active()
        self.tools.record['source_sha256']='d'*64;self.tools.fail='compile'
        with self.job('failed') as job,self.assertRaisesRegex(ValueError,'compile'):
            execute_install(job,self.root,self.source,services=self.tools)
        self.assertEqual(self.store.read_active(),previous)
        self.assertEqual(self.saved.read_bytes(),b'keep settings')

    def test_native_verification_failure_never_selects_an_executable(self):
        self.tools.fail='verify'
        with self.job('failed') as job,self.assertRaisesRegex(ValueError,'verification'):
            execute_install(job,self.root,self.source,services=self.tools)
        self.assertIsNone(self.store.read_active())

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
        original=self.tools.run
        def change(arguments):
            result=original(arguments)
            if 'tools/build_native.py' in arguments:self.tools.record['source_sha256']='e'*64
            return result
        with self.job('changed') as job,patch.object(self.tools,'run',side_effect=change):
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
