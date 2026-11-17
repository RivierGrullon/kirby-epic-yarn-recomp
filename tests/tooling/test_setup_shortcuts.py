# SPDX-License-Identifier: GPL-3.0-only
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
try:
    from PyQt6.QtCore import QFile
except ImportError:
    QFile=None


@unittest.skipIf(QFile is None,'Install tools/requirements-setup.txt for shortcut tests')
class SetupShortcutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PyQt6.QtWidgets import QApplication
        cls.app=QApplication.instance() or QApplication([])

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='Kirby shortcuts ');self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name).resolve();self.destination=self.base/'My Games'
        self.root=self.destination/'.kirby'/('a'*12)
        self.launcher=self.root/'build/native'/('kirby-launcher.exe' if os.name=='nt' else 'kirby-launcher')
        self.launcher.parent.mkdir(parents=True);self.launcher.write_bytes(b'launcher')
        from installer.setup_layout import finish_destination
        finish_destination(self.root,self.destination)
        self.applications=self.base/'Applications';self.desktop=self.base/'Escritorio'

    def create(self,root=None,desktop=False):
        from installer.setup_shortcuts import create_shortcuts
        return create_shortcuts(root or self.root,self.destination,desktop=desktop,
            applications=self.applications,desktop_directory=self.desktop)

    def test_menu_shortcut_targets_persistent_launcher_and_desktop_is_optional(self):
        paths=self.create()
        self.assertEqual(len(paths),1);self.assertTrue(paths[0].is_file())
        self.assertFalse(self.desktop.exists())
        paths=self.create(desktop=True)
        self.assertEqual(len(list(self.applications.iterdir())),1)
        self.assertEqual(len(list(self.desktop.iterdir())),1)
        for path in paths:
            if os.name=='nt':
                self.assertEqual(Path(QFile.symLinkTarget(path.as_posix())).resolve(),self.launcher)
                self.assertEqual(path.suffix,'.lnk')
            else:
                self.assertIn('Terminal=false',path.read_text())
                if shutil.which('desktop-file-validate'):
                    subprocess.run(['desktop-file-validate',str(path)],check=True,capture_output=True)
        if os.name!='nt':self.assertTrue(paths[1].stat().st_mode & 0o111)

    def test_repair_replaces_menu_target_instead_of_adding_another_entry(self):
        first=self.create()[0]
        newer=self.destination/'.kirby'/('b'*12)
        launcher=newer/'build/native'/self.launcher.name;launcher.parent.mkdir(parents=True)
        launcher.write_bytes(b'new launcher')
        from installer.setup_layout import finish_destination
        finish_destination(newer,self.destination)
        self.assertEqual(self.create(newer),[first])
        self.assertEqual(len(list(self.applications.iterdir())),1)
        if os.name=='nt':self.assertEqual(Path(QFile.symLinkTarget(first.as_posix())).resolve(),launcher)
        else:self.assertIn(str(newer),first.read_text())

    def test_missing_launcher_never_creates_menu_or_desktop_entries(self):
        self.launcher.unlink()
        with self.assertRaises(ValueError):self.create(desktop=True)
        self.assertFalse(self.applications.exists());self.assertFalse(self.desktop.exists())

    @unittest.skipIf(os.name=='nt' or shutil.which('gio') is None,'Requires the Linux desktop entry launcher')
    def test_desktop_entry_launches_paths_with_spaces_quotes_dollars_and_percent_literally(self):
        import shlex
        self.destination=self.base/'Games $ 100% \' " ` \\'
        self.root=self.destination/'.kirby'/('c'*12)
        self.launcher=self.root/'build/native/kirby-launcher';self.launcher.parent.mkdir(parents=True)
        output=self.base/'launched'
        self.launcher.write_text('#!/bin/sh\nprintf "%s" "$0" > '+shlex.quote(str(output))+'\n')
        self.launcher.chmod(0o755)
        from installer.setup_layout import finish_destination
        finish_destination(self.root,self.destination)
        shortcut=self.create()[0]
        subprocess.run(['gio','launch',str(shortcut)],check=True,capture_output=True,timeout=10)
        deadline=time.monotonic()+5
        while not output.exists() and time.monotonic()<deadline:time.sleep(.02)
        self.assertEqual(output.read_text(),str(self.launcher))


if __name__=='__main__':unittest.main()
