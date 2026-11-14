# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in real Podman ownership and cancellation checks; no game is launched."""
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
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from installer.container import recover_containers,run_container,toolchain_image
from installer.jobs import Cancelled,Job


@unittest.skipUnless(os.environ.get('KIRBY_CONTAINER_TEST'),'Set KIRBY_CONTAINER_TEST for real Podman tests')
class ManagedContainerTests(unittest.TestCase):
    def setUp(self):
        self.directory=Path(tempfile.mkdtemp(prefix='stage75-container-',dir=ROOT/'local'))
        self.addCleanup(lambda:shutil.rmtree(self.directory))

    def test_actual_managed_tool_runs_by_immutable_image_and_leaves_no_container(self):
        with Job(self.directory/'setup','real-success',lambda event:None) as job:
            image=toolchain_image(job)
            result=run_container(job,ROOT,['python3','-c',"print('managed tool completed')"],image_id=image,capture_limit=4096)
        self.assertEqual(result.output,b'managed tool completed\n')
        self.assertEqual(result.image_id,image)
        self.assertEqual(list((self.directory/'setup/staging').iterdir()),[])

    def test_next_job_recovers_an_actual_container_left_by_an_older_worker(self):
        with Job(self.directory/'setup','new-worker',lambda event:None) as job:
            image=toolchain_image(job)
            token=uuid.uuid4().hex
            directory=job.workspace/'staging/container-recovery';directory.mkdir()
            receipt=dict(version=1,job_id='old-worker',token=token,image_id=image,name='kirby-setup-'+token)
            (directory/'ownership.json').write_text(json.dumps(receipt))
            container=subprocess.check_output(['podman','run','--detach','--rm','--name',receipt['name'],
                                               '--label','io.kirby.setup-job=old-worker','--label','io.kirby.setup-token='+token,
                                               image,'python3','-c','import time;time.sleep(120)'],text=True).strip()
            try:
                self.assertEqual(subprocess.run(['podman','container','exists',container],capture_output=True).returncode,0)
                recover_containers(job)
                self.assertNotEqual(subprocess.run(['podman','container','exists',container],capture_output=True).returncode,0)
                self.assertFalse(directory.exists())
            finally:
                subprocess.run(['podman','rm','--force','--ignore','--time','1',container],capture_output=True,check=True,timeout=8)

    def test_ignored_signals_and_compiler_children_are_removed_without_touching_another_container(self):
        marker=self.directory/'ready'
        heartbeat=self.directory/'heartbeat'
        inside=Path('/workspace')/self.directory.relative_to(ROOT)
        child=("import pathlib,signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);"
               f"p=pathlib.Path({str(inside/'heartbeat')!r});\n"
               "while True:\n with p.open('ab') as f:f.write(b'x')\n time.sleep(.05)\n")
        command=("import pathlib,signal,subprocess,sys,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);"
                 f"subprocess.Popen([sys.executable,'-c',{child!r}]);"
                 f"pathlib.Path({str(inside/'ready')!r}).write_text('ready');time.sleep(120)")
        receipt={}
        control_token=uuid.uuid4().hex
        with Job(self.directory/'setup','real-cancel',lambda event:None) as job:
            image=toolchain_image(job)
            control=subprocess.check_output(['podman','run','--detach','--rm','--label','io.kirby.control-test='+control_token,
                                             image,'python3','-c','import time;time.sleep(120)'],text=True).strip()
            try:
                def cancel_when_running():
                    deadline=time.monotonic()+10
                    while time.monotonic()<deadline:
                        if marker.exists() and heartbeat.exists() and heartbeat.stat().st_size:
                            paths=list((job.workspace/'staging').glob('container-*/ownership.json'))
                            if paths:
                                receipt.update(json.loads(paths[0].read_text()));job.cancel.set();return
                        time.sleep(0.01)
                    job.cancel.set()
                thread=threading.Thread(target=cancel_when_running);thread.start()
                started=time.monotonic()
                try:
                    with self.assertRaises(Cancelled):
                        run_container(job,ROOT,['python3','-c',command],image_id=image)
                finally:thread.join()
                self.assertTrue(receipt,'Container never reached its running child')
                self.assertLess(time.monotonic()-started,10)
                length=heartbeat.stat().st_size;time.sleep(0.25)
                self.assertEqual(heartbeat.stat().st_size,length,'A compiler child survived cancellation')
                remaining=subprocess.check_output(['podman','ps','--all','--filter','label=io.kirby.setup-token='+receipt['token'],
                                                   '--format','{{.ID}}'],text=True)
                self.assertEqual(remaining.strip(),'')
                self.assertEqual(subprocess.run(['podman','container','exists',control],capture_output=True).returncode,0)
            finally:
                # This is our separate control container, identified by its own label.
                labels=json.loads(subprocess.check_output(['podman','inspect','--format','{{json .Config.Labels}}',control],text=True))
                self.assertEqual(labels.get('io.kirby.control-test'),control_token)
                subprocess.run(['podman','rm','--force','--ignore','--time','1',control],check=True,capture_output=True,timeout=8)
