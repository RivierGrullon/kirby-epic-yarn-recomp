# SPDX-License-Identifier: GPL-3.0-only
import json
import os
from pathlib import Path
import sys
import tempfile
import shutil
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.setup_layout import Bundle, prepare_destination, finish_destination, update_game_data


class SetupLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='kirby setup layout ');self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.project=self.base/'bundle/project'
        (self.project/'tools').mkdir(parents=True);(self.project/'tools/kirby.py').write_text('# source')
        (self.project/'.git').mkdir();(self.project/'.git/HEAD').write_text('a'*40+'\n')
        self.runtime=self.base/'bundle/tools/runtime';self.runtime.mkdir(parents=True)
        self.frontend=self.runtime.parent/'qt-launcher'
        files=('kirby-ui.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','platforms/qwindows.dll','platforms/qoffscreen.dll') if os.name=='nt' else (
            'kirby-ui','qt.conf','lib/libQt6Core.so.6','lib/libQt6Gui.so.6','lib/libQt6Widgets.so.6',
            'plugins/platforms/libqxcb.so','plugins/platforms/libqoffscreen.so')
        self.ui_name=files[0]
        for name in files:
            path=self.frontend/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'Qt fixture')
        (self.frontend/self.ui_name).chmod(0o755)
        (self.runtime/'toolchain-download.json').write_text('{}')
        (self.runtime/'ca-certificates.crt').write_bytes(b'certificates')
        self.launcher_name='kirby-launcher.exe' if os.name=='nt' else 'kirby-launcher'
        launcher=self.project/'build/native'/self.launcher_name;launcher.parent.mkdir(parents=True)
        launcher.write_bytes(b'launcher');launcher.chmod(0o755)
        self.libs=self.base/'bundle/host-libs';self.libs.mkdir();(self.libs/'libfixture.so').write_bytes(b'library')
        self.bundle=Bundle(self.project,self.runtime,self.libs,'a'*40,packaged=True)
        self.destination=self.base/"My Games ' literal"

    def test_preparation_copies_authored_sources_and_host_libraries_but_never_developer_data(self):
        for name in ('disc/game.wbfs','generated/game.cpp','local/user/save','vendor/game-source'):
            path=self.project/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'private')
        root=prepare_destination(self.bundle,self.destination)
        self.assertTrue((root/'.git/HEAD').is_file())
        launcher=root/'build/native'/self.launcher_name
        self.assertFalse(launcher.exists())
        self.assertEqual((root/'local/host-libs/libfixture.so').read_bytes(),b'library')
        self.assertFalse((root/'local/host-libs').is_symlink())
        self.assertTrue((root/'tools/runtime/ca-certificates.crt').is_file())
        for name in ('disc','generated','vendor'):self.assertFalse((root/name).exists())
        self.assertFalse((root/'local/user/save').exists())
        for name in ('Play Kirby.sh','Play Kirby.cmd'):
            self.assertFalse((self.destination/name).exists())

    def test_retry_preserves_installed_saves_and_outputs(self):
        root=prepare_destination(self.bundle,self.destination)
        saved=root/'local/user/save';saved.parent.mkdir(parents=True);saved.write_bytes(b'keep')
        self.assertEqual(prepare_destination(self.bundle,self.destination),root)
        self.assertEqual(saved.read_bytes(),b'keep')

    def test_shortcut_is_written_only_for_an_existing_launcher_and_survives_bundle_removal(self):
        root=prepare_destination(self.bundle,self.destination)
        with self.assertRaisesRegex(ValueError, 'launcher is missing'):
            finish_destination(root,self.destination)
        (root/'build/native').mkdir(parents=True,exist_ok=True)
        (root/'build/native'/self.launcher_name).touch()
        shortcut=finish_destination(root,self.destination)
        self.assertTrue(shortcut.is_file())
        if os.name=='nt':
            self.assertEqual(shortcut.name,'Play Kirby.cmd')
            self.assertIn(str(Path('.kirby')/('a'*12)/'build/native'/self.launcher_name),shortcut.read_text())
        else:
            self.assertIn('.kirby/',shortcut.read_text())
            self.assertNotIn(str(self.project),shortcut.read_text())
            self.assertTrue(shortcut.stat().st_mode & 0o111)

    def test_bundled_frontend_survives_unmount_and_missing_file_is_repaired(self):
        root=prepare_destination(self.bundle,self.destination)
        program=root/'build/native/qt-launcher'/self.ui_name
        self.assertEqual(program.read_bytes(),b'Qt fixture')
        program.unlink()
        self.assertEqual(prepare_destination(self.bundle,self.destination),root)
        shutil.rmtree(self.frontend)
        self.assertEqual(program.read_bytes(),b'Qt fixture')
        self.assertEqual((program.parent/'.bundled-revision').read_text().strip(),self.bundle.revision)

    def test_incomplete_release_cannot_publish_a_partial_destination(self):
        (self.frontend/self.ui_name).unlink()
        with self.assertRaisesRegex(ValueError,'Incomplete Qt launcher'):
            prepare_destination(self.bundle,self.destination)
        self.assertFalse((self.destination/'.kirby'/self.bundle.revision[:12]).exists())

    def test_destination_cannot_recurse_into_sources_or_follow_a_symlink(self):
        with self.assertRaises(ValueError):prepare_destination(self.bundle,self.project/'destination')
        if os.name=='nt':self.skipTest('Symlink creation requires Windows developer privileges')
        self.destination.mkdir();(self.destination/'.kirby').symlink_to(self.project,target_is_directory=True)
        with self.assertRaises(ValueError):prepare_destination(self.bundle,self.destination)

    def test_data_path_update_preserves_other_settings_and_comments(self):
        root=prepare_destination(self.bundle,self.destination)
        settings=root/'local/user/settings.toml';settings.parent.mkdir(parents=True)
        settings.write_text('# keep my settings\n[paths]\ndvd_root = "old"\n\n[audio]\nvolume = 0.5\n')
        update_game_data(root,self.base/"new ' DATA")
        self.assertIn('# keep my settings',settings.read_text())
        import tomllib
        value=tomllib.loads(settings.read_text())
        self.assertEqual(value['paths']['dvd_root'],str(self.base/"new ' DATA"))
        self.assertEqual(value['audio']['volume'],.5)

    def test_packaged_runtime_symlink_is_never_retained_or_modified(self):
        if os.name=='nt':self.skipTest('Symlink creation requires Windows developer privileges')
        external=self.base/'developer-tools';external.mkdir();(external/'toolchain-download.json').write_text('private')
        (self.project/'tools/runtime').symlink_to(external,target_is_directory=True)
        root=prepare_destination(self.bundle,self.destination)
        self.assertFalse((root/'tools/runtime').is_symlink())
        self.assertEqual((root/'tools/runtime/toolchain-download.json').read_text(),'{}')
        self.assertEqual((external/'toolchain-download.json').read_text(),'private')


if __name__=='__main__':unittest.main()
