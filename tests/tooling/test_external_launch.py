# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer import external_launch as external
from installer import installations
from installer.installations import InstallationStore
from installer.platforms import executable_name
from installer.setup_layout import Bundle
from launcher_bridge import main as bridge_main
from launcher_profiles import write_profile

REPO=Path(__file__).resolve().parents[2]


class ExternalLaunchTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="Kirby bridge ' $ ")
        self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name)
        self.destination=self.base/'Game'
        self.dsp=bytes(4096)
        self.dsp_hash=hashlib.sha256(self.dsp).hexdigest()
        pin=patch.object(installations,'DSP_COEF_SHA256',self.dsp_hash)
        pin.start();self.addCleanup(pin.stop)

    def install(self, revision='a'):
        root=self.destination/'.kirby'/(revision*12)
        candidate=root/'candidate';candidate.mkdir(parents=True)
        body=b'#!/bin/sh\nprintf "%s\\n" "$PWD" "$@" "$LD_LIBRARY_PATH"\nexit 7\n'
        (candidate/executable_name()).write_bytes(body)
        (candidate/'dsp_coef.bin').write_bytes(self.dsp)
        manifest=dict(version=1,game_id='RK5E01',revision=0,dol_sha1='f4280a2b0709bfde4132be22e9f7fa8ecfa2a130',
                      data_root=str(self.base/('DATA '+revision)),project_sha256='1'*64,translation_key='2'*64,
                      toolchain_image_id='3'*64,verification=['bootstrap','init','disc','es','video'],
                      executable=dict(file=executable_name(),size=len(body),sha256=hashlib.sha256(body).hexdigest()),
                      resources=[dict(file='dsp_coef.bin',size=4096,sha256=self.dsp_hash)])
        store=InstallationStore(root)
        identifier=store.publish(candidate,manifest);store.activate(identifier)
        external.publish(root,self.destination,manifest['data_root'])
        return root,store,identifier

    def test_updates_keep_user_settings_and_nand_in_the_same_directory(self):
        self.install()
        settings=self.destination/'local/user/settings.toml'
        with settings.open('a') as stream:stream.write('\n# keep preferences\n[video]\nfullscreen = true\n')
        nand=self.destination/'local/user/nand/save'
        nand.parent.mkdir(parents=True);nand.write_bytes(b'player save')
        root,_,_=self.install('b')
        selected,_,_=external.selection(self.destination)
        self.assertEqual(selected,root)
        document=tomllib.loads(settings.read_text())
        self.assertTrue(document['video']['fullscreen'])
        self.assertEqual(document['paths']['dvd_root'],str(self.base/'DATA b'))
        self.assertIn('# keep preferences',settings.read_text())
        self.assertEqual(nand.read_bytes(),b'player save')

    def test_tampered_executable_is_rejected_before_launch(self):
        _,store,identifier=self.install()
        exe=store.executable(identifier);exe.chmod(0o700);exe.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'digest|size'):external.play_command(self.destination)

    def test_marker_cannot_select_an_arbitrary_path_or_invalid_document(self):
        self.destination.mkdir()
        for value in ([],dict(version=1,project='../outside'),dict(version=1,project='/tmp/other')):
            (self.destination/external.MARKER).write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError,'selection'):external.selection(self.destination)

    def test_big_picture_adds_fullscreen_and_explicit_windowed_has_last_precedence(self):
        self.install()
        with patch.dict(os.environ,{'RECOMPLAUNCHER_MODE':'bigpicture'}):
            command,_,cwd=external.play_command(self.destination,['--windowed'])
        self.assertEqual(command[-2:],['--fullscreen','--windowed'])
        self.assertEqual(cwd,self.destination)

    @unittest.skipIf(os.name=='nt','POSIX synthetic executable')
    def test_play_waits_for_the_game_and_preserves_its_exit_code(self):
        self.install()
        self.assertEqual(external.play(self.destination),7)
        log=(self.destination/'logs/game.log').read_text()
        self.assertIn(str(self.destination),log)
        self.assertIn('--config',log)

    @unittest.skipIf(os.name=='nt','POSIX synthetic executable')
    def test_play_moves_the_previous_log_aside(self):
        self.install()
        logs=self.destination/'logs';logs.mkdir()
        (logs/'game.log').write_text('previous run')
        external.play(self.destination)
        self.assertEqual((logs/'game.1.log').read_text(),'previous run')
        self.assertIn('--config',(logs/'game.log').read_text())

    def test_only_the_last_five_logs_are_kept(self):
        logs=self.base/'logs';logs.mkdir()
        names=['game.log',*(f'game.{number}.log' for number in range(1,5))]
        for name in names:(logs/name).write_text(name)
        external.keep_older_logs(logs)
        self.assertEqual((logs/'game.4.log').read_text(),'game.3.log')
        self.assertEqual((logs/'game.1.log').read_text(),'game.log')
        self.assertFalse((logs/'game.log').exists())
        self.assertFalse((logs/'game.5.log').exists())

    def test_bridge_exits_with_its_own_code_when_nothing_is_set_up(self):
        command=[sys.executable,str(REPO/'tools/launcher_bridge.py'),'--play','--destination',str(self.destination)]
        result=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(result.returncode,20,result.stderr)
        self.assertIn('Run Kirby Setup before playing',result.stderr)

    def test_profile_explains_the_bridge_exit_codes(self):
        profile=json.loads((REPO/'recomplauncher.json').read_text())
        self.assertEqual(sorted(profile['exit_codes']),['20','21'])

    @unittest.skipIf(os.name=='nt','Linux shared-library search path')
    def test_bridge_uses_package_libraries_for_an_existing_installation(self):
        root,_,_=self.install()
        libraries=self.base/'host-libs'
        libraries.mkdir()
        (self.base/'revision').write_text('b'*40)
        with patch.dict(os.environ,{'LD_LIBRARY_PATH':'/frozen/python:/original',
                                    'LD_LIBRARY_PATH_ORIG':'/original'}):
            code=bridge_main(['--appdir',str(self.base),'--play','--destination',str(self.destination)])
        self.assertEqual(code,7)
        log=(self.destination/'logs/game.log').read_text().splitlines()
        self.assertEqual(log[-1],os.pathsep.join((str(root/'local/host-libs'),str(libraries),'/original')))

    def test_packaged_profile_uses_the_exact_source_revision(self):
        write_profile(REPO,self.base,'e'*40)
        profile=json.loads((self.base/'recomplauncher.json').read_text())
        self.assertEqual(profile['prepare']['version'],'e'*40)
        self.assertNotIn('saves',profile)  # NTFS-stream backups are not certified.
        self.assertNotIn('dumps',profile['game'])  # DOL SHA-1 is not an ISO checksum.

    def test_missing_source_never_starts_or_publishes_a_build(self):
        bundle=Bundle(self.base,self.base,self.base,'a'*40)
        with patch.object(external,'prepare_destination') as prepare:
            with self.assertRaisesRegex(ValueError,'existing'):external.prepare(bundle,self.destination,self.base/'absent.iso')
            prepare.assert_not_called()
        self.assertFalse((self.destination/external.MARKER).exists())

    def test_bridge_help_and_incomplete_prepare_work_without_qt(self):
        command=[sys.executable,str(REPO/'tools/launcher_bridge.py')]
        result=subprocess.run([*command,'--bridge-help'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('--prepare',result.stdout)
        result=subprocess.run([*command,'--prepare'],capture_output=True,text=True)
        self.assertEqual(result.returncode,2)
        self.assertIn('requires --game and --destination',result.stderr)

    def test_prepare_never_publishes_after_a_worker_error_or_nonzero_exit(self):
        root=self.base/'workspace';(root/'tools').mkdir(parents=True)
        source=self.base/'own.iso';source.touch()
        script=root/'tools/install.py'
        script.write_text('import json,sys\nprint(json.dumps(dict(version=1,job_id="test",sequence=1,'
                          'stage="install",state="failed",error="Synthetic build failure")),flush=True)\nsys.exit(1)\n')
        bundle=Bundle(root,self.base/'runtime',self.base/'libs','a'*40)
        with patch.object(external,'prepare_destination',return_value=root),patch.object(external,'publish') as publish:
            with self.assertRaisesRegex(ValueError,'Synthetic build failure'):
                external.prepare(bundle,self.destination,source)
            publish.assert_not_called()

    def test_prepare_reports_setup_progress_that_never_moves_back(self):
        root=self.base/'workspace';(root/'tools').mkdir(parents=True)
        source=self.base/'own.iso';source.touch()
        events=[dict(stage='prepare',state='progress',completed=50,total=100),
                dict(stage='prepare',state='progress',completed=10,total=100),
                dict(stage='compile',state='progress',completed=1,total=2,unit='tasks'),
                dict(stage='install',state='failed',error='Synthetic build failure')]
        lines=[json.dumps(dict(event,version=1,job_id='test',sequence=number)) for number,event in enumerate(events,1)]
        (root/'tools/install.py').write_text('import sys\nprint('+repr('\n'.join(lines))+',flush=True)\nsys.exit(1)\n')
        bundle=Bundle(root,self.base/'runtime',self.base/'libs','a'*40)
        output=io.StringIO()
        with patch.object(external,'prepare_destination',return_value=root),contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(ValueError,'Synthetic build failure'):
                external.prepare(bundle,self.destination,source)
        fractions=[line.split()[1] for line in output.getvalue().splitlines()]
        self.assertEqual(fractions,['0.10','0.10','0.70','0.70'])

    @unittest.skipIf(os.name=='nt','POSIX AppRun dispatch')
    def test_apprun_keeps_normal_setup_and_dispatches_bridge_flags_literally(self):
        appdir=self.base/'AppDir';appdir.mkdir()
        entry=appdir/'AppRun';entry.write_bytes((REPO/'packaging/linux/AppRun').read_bytes());entry.chmod(0o755)
        for path,label in (('tools/setup/kirby-setup','gui'),('tools/bridge/Kirby-Bridge','bridge')):
            program=appdir/path;program.parent.mkdir(parents=True)
            program.write_text('#!/bin/sh\nprintf "%s\\n" '+label+' "$@"\n');program.chmod(0o755)
        normal=subprocess.check_output([entry,'--self-test'],text=True).splitlines()
        self.assertEqual(normal,['gui','--appdir',str(appdir),'--self-test'])
        bridged=subprocess.check_output([entry,'--prepare','--game',str(self.base/"own ' $.rvz")],text=True).splitlines()
        self.assertEqual(bridged,['bridge','--appdir',str(appdir),'--prepare','--game',str(self.base/"own ' $.rvz")])


if __name__=='__main__':unittest.main()
