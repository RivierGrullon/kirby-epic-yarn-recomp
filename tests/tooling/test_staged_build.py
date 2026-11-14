# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import io
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import build_native
import kirby
from build_layout import BuildLayout


class StagedBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="kirby's staged build ")
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.layout=BuildLayout(self.root,self.root/'local/setup/builds/job-01')

    def test_function_map_and_logs_stay_in_the_stage(self):
        output=io.StringIO()
        command=[sys.executable,'-c',"print('tool output')"]
        with contextlib.redirect_stdout(output):
            kirby.write_map([(0x80004000,'example_function',4)],layout=self.layout)
            kirby.run_logged('probe',command,layout=self.layout)
        self.assertEqual((self.layout.metadata/'RK5E01-functions.txt').read_text(),'80004000 example_function\n')
        self.assertEqual((self.layout.logs/'probe.log').read_text(),'tool output\n')
        self.assertFalse((self.root/'local/RK5E01-functions.txt').exists())
        self.assertFalse((self.root/'build-logs').exists())
        logged=next(line[2:] for line in output.getvalue().splitlines() if line.startswith('+ '))
        self.assertEqual(shlex.split(logged),command)

    def test_profile_keeps_authored_sources_and_redirects_generated_files(self):
        profile=(Path(__file__).resolve().parents[2]/'projects/RK5E01/recomp.yml').read_text()
        dol=self.root/"disc's selected/DATA/sys/main.dol"
        text=kirby.render_profile(profile,dol,self.layout)
        for value in (self.root,dol,self.layout.generated,self.layout.metadata/'RK5E01-functions.txt',
                      self.layout.metadata/'native-index.json'):
            self.assertIn(json.dumps(str(value)),text)
        self.assertIn('native_registration_root: runtime/native-src',text)
        self.assertNotIn('root: generated\n',text)

    def test_missing_profile_path_contract_fails_before_writing(self):
        with self.assertRaises(ValueError):
            kirby.render_profile('workspace_root: ../..\n',self.root/'main.dol',self.layout)
        self.assertEqual(list(self.root.iterdir()),[])

    def test_translator_keeps_tool_scratch_paths_safe_and_publishes_to_quoted_stage(self):
        tools=self.root/'test-tools';tools.mkdir()
        dotnet=tools/'dotnet'
        dotnet.write_text(f'#!{sys.executable}\nimport pathlib,sys\n'
                          "a=sys.argv[1:];p=pathlib.Path(a[a.index('--artifacts-path')+1])\n"
                          'if "\'" in str(p): sys.exit(3)\n'
                          "target=p/'bin/Translator.Cli/release/Translator.Cli.dll'\n"
                          "target.parent.mkdir(parents=True);target.write_bytes(b'staged translator')\n")
        dotnet.chmod(0o700)
        with patch.dict(os.environ,PATH=str(tools)+os.pathsep+os.environ['PATH']), \
                contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            command=kirby.translator(layout=self.layout)
        self.assertEqual(command[0],'dotnet')
        self.assertTrue(command[1].is_relative_to(self.layout.stage))
        self.assertEqual(command[1].read_bytes(),b'staged translator')

    def test_build_helper_configures_and_links_only_inside_its_stage(self):
        self.layout.ensure()
        (self.layout.generated/'build_shards').mkdir()
        (self.layout.generated/'build_shards/shards.cmake').write_text('# synthetic graph\n')
        (self.layout.generated/'KirbyBootConfig.h').write_text('// synthetic profile\n')
        for path in ('runtime','aurora-main'):
            target=self.root/'vendor/wiicompiled'/path;target.mkdir(parents=True)
            (target/'CMakeLists.txt').write_text('# synthetic tool fixture\n')
        (self.root/'vendor/wiicompiled/LICENSE').write_text('synthetic license fixture\n')
        (self.root/'dependencies.json').write_text(json.dumps({'wiicompiled':{
            'commit':'a'*40,'submodules':{'aurora-main':'b'*40}}}))
        production=self.root/'build/native/WiiCompiled';production.parent.mkdir(parents=True)
        production.write_bytes(b'keep runnable build')
        tools=self.root/'test-tools';tools.mkdir()
        cmake=tools/'cmake'
        cmake.write_text(f'#!{sys.executable}\nimport json,pathlib,sys\na=sys.argv[1:]\n'
                         "if '--build' in a:\n"
                         " p=pathlib.Path(a[a.index('--build')+1]);(p/'WiiCompiled').write_bytes(b'staged build')\n"
                         "else:\n"
                         " p=pathlib.Path(a[a.index('-B')+1]);p.mkdir(parents=True,exist_ok=True);(p/'configure.json').write_text(json.dumps(a))\n")
        cmake.chmod(0o700)
        with patch.object(build_native,'ROOT',self.root), \
             patch.dict(os.environ,PATH=str(tools)+os.pathsep+os.environ['PATH']), \
             patch.object(sys,'argv',['build_native.py','--stage-root',str(self.layout.stage)]), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(build_native.main(),0)
        self.assertEqual(production.read_bytes(),b'keep runnable build')
        self.assertEqual((self.layout.build/'WiiCompiled').read_bytes(),b'staged build')
        configured=json.loads((self.layout.build/'configure.json').read_text())
        self.assertIn('-DKIRBY_GENERATED_ROOT='+str(self.layout.generated),configured)
        self.assertIn('-DCMAKE_RUNTIME_OUTPUT_DIRECTORY='+str(self.layout.build),configured)
        self.assertEqual((self.layout.workspace/'WiiCompiled.LICENSE').read_text(),'synthetic license fixture\n')
        self.assertEqual((self.layout.workspace/'generated').resolve(),self.layout.generated)
