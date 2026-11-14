# SPDX-License-Identifier: GPL-3.0-only
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from build_layout import BuildLayout


class BuildLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="kirby's build layout ")
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)

    def test_default_layout_keeps_developer_paths(self):
        layout=BuildLayout(self.root)
        self.assertEqual(layout.generated,self.root/'generated')
        self.assertEqual(layout.metadata,self.root/'local')
        self.assertEqual(layout.workspace,self.root/'local/native-workspace')
        self.assertEqual(layout.build,self.root/'build/native')
        self.assertEqual(list(self.root.iterdir()),[])

    def test_stage_owns_all_mutable_build_outputs_and_resumes(self):
        stage=self.root/'local/setup/builds/job-01'
        layout=BuildLayout(self.root,stage)
        layout.ensure()
        for path in (layout.generated,layout.metadata,layout.logs,layout.workspace,
                     layout.build,layout.translator_artifacts):
            self.assertTrue(path.is_relative_to(stage))
        self.assertFalse((self.root/'generated').exists())
        self.assertFalse((self.root/'build').exists())
        sentinel=layout.generated/'keep.txt';sentinel.write_text('keep')
        BuildLayout(self.root,stage).ensure()
        self.assertEqual(sentinel.read_text(),'keep')

    def test_refuses_user_data_developer_build_and_sources_as_stage(self):
        for path in ('local/user','build/native','generated','local/native-workspace',
                     'local/setup/sources/hash','local/setup/builds','local/setup/builds/../sources/hash'):
            with self.assertRaises(ValueError,msg=path):
                BuildLayout(self.root,self.root/path)
        self.assertEqual(list(self.root.iterdir()),[])

    def test_job_directory_ids_avoid_build_tool_dependency_parser_characters(self):
        for name in ("job's stage","has space",'nested/job','job;other','job$variable','jób','x'*81):
            with self.assertRaisesRegex(ValueError,'job ID',msg=name):
                BuildLayout(self.root,self.root/'local/setup/builds'/name)

    def test_refuses_unowned_nonempty_staging_directory(self):
        stage=self.root/'local/setup/builds/existing';stage.mkdir(parents=True)
        sentinel=stage/'save';sentinel.write_text('keep')
        with self.assertRaisesRegex(ValueError,'unowned'):
            BuildLayout(self.root,stage).ensure()
        self.assertEqual(list(stage.iterdir()),[sentinel])
        self.assertEqual(sentinel.read_text(),'keep')

    def test_rejects_symlink_stage_or_parent_before_writing(self):
        base=self.root/'local/setup/builds';base.mkdir(parents=True)
        outside=self.root/'other';outside.mkdir()
        (base/'link').symlink_to(outside,target_is_directory=True)
        with self.assertRaises(ValueError):
            BuildLayout(self.root,base/'link').ensure()
        (self.root/'local/setup').rename(self.root/'local/saved-setup')
        (self.root/'local/setup').symlink_to(outside,target_is_directory=True)
        with self.assertRaises(ValueError):
            BuildLayout(self.root,base/'job').ensure()
        self.assertEqual(list(outside.iterdir()),[])

    def test_rejects_a_mismatched_existing_layout_record(self):
        stage=self.root/'local/setup/builds/job';layout=BuildLayout(self.root,stage);layout.ensure()
        marker=stage/'.layout.json'
        record=json.loads(marker.read_text());record['project_identity']['inode']+=1
        marker.write_text(json.dumps(record))
        with self.assertRaises(ValueError):
            layout.ensure()
