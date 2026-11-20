from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from windows import package_windows


class WindowsPackageTests(unittest.TestCase):
    def fixture(self, root):
        build = root / 'build/native'
        build.mkdir(parents=True)
        for name in package_windows.REQUIRED:
            (build / name).write_bytes(b'fixture')
        for name in ('LICENSE', 'THIRD_PARTY.md', 'dependencies.json', 'redist.exe'):
            (root / name).write_text('fixture')
        return root / 'redist.exe'

    def test_package_excludes_user_data_and_build_intermediates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            redist = self.fixture(root)
            for name in ('disc/DATA/sys/main.dol', 'local/user/settings.toml',
                         'build/native/test.exe', 'build/native/object.obj', 'build/native/cache.db'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('private')
            with patch.object(package_windows, 'check_pe'), patch.object(package_windows, 'verify_package'):
                output = package_windows.package(root, redist)
            with zipfile.ZipFile(output) as archive:
                self.assertIsNone(archive.testzip())
                self.assertIn('kirby-portable.txt', archive.namelist())
                self.assertIn('vc_redist.x64.exe', archive.namelist())
                self.assertFalse(any(archive.read(name) == b'private' for name in archive.namelist()))

    def test_missing_runtime_keeps_previous_zip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            redist = self.fixture(root)
            output = root / 'previous.zip'
            output.write_bytes(b'previous')
            (root / 'build/native/SDL3.dll').unlink()
            with self.assertRaisesRegex(ValueError, 'Missing build output'):
                package_windows.package(root, redist, output)
            self.assertEqual(output.read_bytes(), b'previous')

    def test_qt_frontend_ships_with_its_platform_plugin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            redist = self.fixture(root)
            qt = root/'build/native/qt-launcher'
            for name in ('kirby-ui.exe', 'Qt6Widgets.dll', 'platforms/qwindows.dll',
                         'translations/qt_es.qm', 'debug.log'):
                path = qt/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
            with patch.object(package_windows, 'check_pe'), patch.object(package_windows, 'verify_package'):
                output = package_windows.package(root, redist)
            with zipfile.ZipFile(output) as archive:
                self.assertIn('qt-launcher/platforms/qwindows.dll', archive.namelist())
                self.assertIn('qt-launcher/translations/qt_es.qm', archive.namelist())
                self.assertNotIn('qt-launcher/debug.log', archive.namelist())
