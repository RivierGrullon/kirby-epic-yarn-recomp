# SPDX-License-Identifier: GPL-3.0-only
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.jobs import Cancelled,Job,run_process
from installer.container import CleanupError,recover_containers,run_container

ROOT=Path(__file__).resolve().parents[2]


class ContainerJobsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="kirby's container job ")
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.repo=self.root/'repo';(self.repo/'tools').mkdir(parents=True)
        shutil.copy2(ROOT/'tools/container.sh',self.repo/'tools/container.sh')
        self.state_path=self.root/'engine.json'
        self.state_path.write_text(json.dumps(dict(containers={'c'*64:dict(labels={'owner':'someone-else'})})))
        self.bin=self.root/'bin';self.bin.mkdir()
        script=ROOT/'tests/tooling/fixtures/podman.py'
        tool=self.bin/'podman';tool.write_text(f'#!{sys.executable}\n'+script.read_text());tool.chmod(0o700)
        self.env=patch.dict(os.environ,PATH=str(self.bin)+os.pathsep+os.environ['PATH'],
                            KIRBY_TEST_PODMAN_STATE=str(self.state_path))
        self.env.start();self.addCleanup(self.env.stop)

    def state(self):return json.loads(self.state_path.read_text())

    def configure(self,**fields):
        state=self.state();state.update(fields);self.state_path.write_text(json.dumps(state))

    def job(self):return Job(self.root/'setup','job-1',lambda event:None)

    def assert_foreign_only(self):
        self.assertEqual(set(self.state()['containers']),{'c'*64})

    def test_success_preserves_literal_arguments_and_removes_only_owned_container(self):
        literal="a file's name; $(not-a-command)"
        with self.job() as job:
            result=run_container(job,self.repo,['success',literal],capture_limit=4096)
        self.assertEqual(json.loads(result.output),['success',literal])
        self.assertEqual(result.image_id,'a'*64)
        self.assert_foreign_only()
        self.assertEqual(list((self.root/'setup/staging').iterdir()),[])

    def test_tool_failure_cleans_up_the_detached_engine_record(self):
        with self.job() as job,self.assertRaisesRegex(ValueError,'code 7'):
            run_container(job,self.repo,['fail'])
        self.assert_foreign_only()

    def test_full_primary_log_cannot_prevent_container_cleanup(self):
        with self.job() as job,patch.object(job,'write_log',side_effect=ValueError('log limit reached')):
            with self.assertRaisesRegex(ValueError,'log limit'):
                run_container(job,self.repo,['success'],image_id='a'*64)
        self.assert_foreign_only()
        self.assertEqual(list((self.root/'setup/staging').iterdir()),[])

    def test_cancellation_cleans_container_after_client_has_been_killed(self):
        with self.job() as job:
            def cancel_when_created():
                deadline=time.monotonic()+4
                while time.monotonic()<deadline:
                    if 'b'*64 in self.state()['containers']:
                        job.cancel.set();return
                    time.sleep(0.01)
            thread=threading.Thread(target=cancel_when_created);thread.start()
            try:
                with self.assertRaises(Cancelled):run_container(job,self.repo,['hold'])
            finally:thread.join()
        self.assert_foreign_only()
        self.assertEqual(list((self.root/'setup/staging').iterdir()),[])

    def test_missing_or_foreign_cid_file_does_not_prevent_label_based_cleanup(self):
        for option in ('missing_cid','wrong_cid'):
            with self.subTest(option=option):
                self.configure(**{option:True})
                with Job(self.root/option,'job-1',lambda event:None) as job:
                    run_container(job,self.repo,['success'])
                self.assert_foreign_only()

    def test_cleanup_failure_retains_receipt_and_is_not_reported_as_success(self):
        self.configure(fail_cleanup=True)
        with self.job() as job,self.assertRaises(CleanupError):
            run_container(job,self.repo,['success'])
        self.assertIn('b'*64,self.state()['containers'])
        receipts=list((self.root/'setup/staging').glob('container-*/ownership.json'))
        self.assertEqual(len(receipts),1)
        receipt=json.loads(receipts[0].read_text())
        self.assertEqual(receipt['job_id'],'job-1')
        self.assertEqual(len(receipt['token']),32)

    def test_recovery_rechecks_engine_state_and_removes_a_previous_failed_job(self):
        self.configure(fail_cleanup=True)
        with self.job() as job,self.assertRaises(CleanupError):
            run_container(job,self.repo,['success'])
        self.configure(fail_cleanup=False)
        with Job(self.root/'setup','recovery',lambda event:None) as job:
            recover_containers(job)
        self.assert_foreign_only()
        self.assertEqual(list((self.root/'setup/staging').iterdir()),[])

    def test_recovery_refuses_invalid_receipts_without_touching_a_foreign_container(self):
        with self.job() as job:
            directory=job.workspace/'staging/container-invalid';directory.mkdir()
            path=directory/'ownership.json';path.write_text(json.dumps(dict(version=1,job_id='../wrong',token='d'*32)))
            with self.assertRaises(CleanupError):recover_containers(job)
            self.assertTrue(path.exists())
        self.assert_foreign_only()

    def test_managed_shell_rejects_invalid_identity_before_running_a_tool(self):
        for identifier in ('../escape','job;other','has space'):
            result=subprocess.run(['bash',str(self.repo/'tools/container.sh'),'--managed',str(self.root/'cid'),
                                   identifier,'d'*32,'a'*64,'--','success'],capture_output=True,text=True)
            self.assertEqual(result.returncode,2,result.stdout+result.stderr)
            self.assert_foreign_only()

    def test_control_deadline_works_even_after_cancellation(self):
        with self.job() as job:
            job.cancel.set()
            started=time.monotonic()
            with self.assertRaises(TimeoutError):
                run_process(job,[sys.executable,'-c','import time;time.sleep(30)'],
                            timeout=0.1,cancellable=False,log_output=False,capture_limit=1024)
            self.assertLess(time.monotonic()-started,3)

    def test_native_probe_can_finish_gracefully_after_observed_output(self):
        output=bytearray()
        script="import signal,sys,time;signal.signal(signal.SIGTERM,lambda *_:sys.exit(0));print('ready',flush=True);time.sleep(30)"
        with self.job() as job:
            result=run_process(job,[sys.executable,'-c',script],timeout=5,capture_limit=1024,
                               on_output=output.extend,stop_when=lambda:b'ready' in output,
                               cwd=self.root,env=dict(os.environ,SDL_AUDIO_DRIVER='dummy'))
        self.assertEqual(result,b'ready\n')
