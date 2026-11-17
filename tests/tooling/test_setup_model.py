# SPDX-License-Identifier: GPL-3.0-only
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.setup_model import Session


class SetupModelTests(unittest.TestCase):
    def setUp(self):
        self.root=Path(tempfile.gettempdir())/'kirby model'
        self.source=self.root/"own ' copy.wbfs"
        self.session=Session('job-1',self.root,self.source)
        self.sequence=0

    def event(self,stage='compile',state='progress',**fields):
        self.sequence+=1
        return dict(version=1,job_id='job-1',sequence=self.sequence,stage=stage,state=state,**fields)

    def success(self):
        from installer.platforms import executable_name
        identifier='a'*64
        return self.event('install','complete',result=dict(installation_id=identifier,committed=True,
            source=str(self.source),data_root=str(self.root/'DATA'),
            executable=str(self.root/'local/setup/installations'/identifier/executable_name())))

    def test_partial_json_lines_preserve_real_progress_and_units(self):
        event=(json.dumps(self.event(completed=7,total=10,unit='tasks'))+'\n').encode()
        self.assertEqual(self.session.feed(event[:20]),[])
        self.session.feed(event[20:])
        self.assertEqual(self.session.progress,70)
        self.assertIn('7 / 10',self.session.progress_text)
        self.assertNotIn('MiB',self.session.progress_text)
        self.session.feed((json.dumps(self.event('translate','started'))+'\n').encode())
        self.assertIsNone(self.session.progress)

    def test_complete_event_waits_for_successful_process_exit(self):
        self.session.accept(self.success())
        self.assertEqual(self.session.status,'running')
        self.session.finish(0)
        self.assertEqual(self.session.status,'ready')
        self.assertEqual(self.session.result['installation_id'],'a'*64)

    def test_crash_or_missing_completion_never_enables_play(self):
        self.session.accept(self.success());self.session.finish(1)
        self.assertEqual(self.session.status,'failed')
        self.assertIsNone(self.session.result)
        other=Session('job-1',self.root,self.source);other.finish(0)
        self.assertEqual(other.status,'failed')

    def test_wrong_identity_sequence_types_and_progress_are_rejected(self):
        for changed in (dict(job_id='other'),dict(sequence=9),dict(completed=True,total=2),
                        dict(completed=3,total=2),dict(completed=1,total=2,unit='guesses')):
            session=Session('job-1',self.root,self.source)
            event=dict(version=1,job_id='job-1',sequence=1,stage='compile',state='progress',**{})
            event.update(changed)
            with self.subTest(changed=changed),self.assertRaises(ValueError): session.accept(event)

    def test_uncommitted_or_other_source_installation_is_rejected(self):
        for field,value in [('committed',False),('source',str(self.root/'other')),('executable',str(self.root/'elsewhere'))]:
            event=self.success();event['sequence']=1;event['result'][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                Session('job-1',self.root,self.source).accept(event)

    def test_disc_result_can_use_the_canonical_path_of_a_selected_symlink(self):
        if os.name=='nt':self.skipTest('Symlink creation requires Windows developer privileges')
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'real.wbfs';source.write_bytes(b'fixture')
            selected=root/'selected.wbfs';selected.symlink_to(source)
            session=Session('job-1',self.root,selected)
            event=self.success();event['result']['source']=str(source)
            session.accept(event);session.finish(0)
            self.assertEqual(session.status,'ready')

    def test_oversized_partial_line_and_events_after_completion_are_rejected(self):
        with self.assertRaises(ValueError):self.session.feed(b'x'*65537)
        self.session=Session('job-1',self.root,self.source)
        self.session.accept(self.success())
        with self.assertRaises(ValueError):self.session.accept(self.event())

    def test_cancelled_exit_keeps_error_and_never_exposes_an_installation(self):
        self.session.accept(self.event('install','cancelled',error='Setup cancelled safely'))
        self.session.finish(2)
        self.assertEqual(self.session.status,'cancelled')
        self.assertIsNone(self.session.result)

    def test_compilation_failure_does_not_mark_compilation_as_completed(self):
        self.session.accept(self.event('compile','progress',completed=1,total=10,unit='tasks'))
        self.session.accept(self.event('install','failed',error='Compiler failed'))
        self.session.finish(1)
        self.assertEqual(self.session.phases[3],'failed')
        self.assertEqual(self.session.phases[4],'pending')


if __name__=='__main__':unittest.main()
