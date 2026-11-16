# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.appimage import prepare_appimage


class AppImageSetupTests(unittest.TestCase):
    def test_read_only_package_uses_a_writable_copy_and_preserves_existing_save(self):
        with tempfile.TemporaryDirectory(prefix='kirby AppImage ') as temporary:
            base=Path(temporary);appdir=base/'mounted app';project=appdir/'project'
            (project/'tools').mkdir(parents=True)
            (project/'tools/kirby.py').write_text('# authored source\n')
            (appdir/'tools/runtime').mkdir(parents=True)
            (appdir/'host-libs').mkdir()
            (appdir/'revision').write_text('a'*40+'\n')
            root=prepare_appimage(appdir,base/'user data')
            self.assertNotEqual(root,project)
            self.assertEqual((root/'tools/kirby.py').read_text(),'# authored source\n')
            save=root/'local/user/save';save.parent.mkdir(parents=True);save.write_bytes(b'keep')
            self.assertEqual(prepare_appimage(appdir,base/'user data'),root)
            self.assertEqual(save.read_bytes(),b'keep')
            self.assertEqual((root/'tools/runtime').resolve(),appdir/'tools/runtime')

    def test_invalid_revision_cannot_select_a_directory_outside_user_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir=Path(temporary)/'image';appdir.mkdir()
            (appdir/'revision').write_text('../../elsewhere')
            with self.assertRaisesRegex(ValueError,'revision'):
                prepare_appimage(appdir,Path(temporary)/'data')


if __name__ == '__main__':unittest.main()
