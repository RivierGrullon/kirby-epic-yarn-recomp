# SPDX-License-Identifier: GPL-3.0-only
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import package_portable as package


class PortablePackageTests(unittest.TestCase):
    def archive(self, items):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode='w') as archive:
            for name, body, link in items:
                entry = tarfile.TarInfo(name)
                if link is not None:
                    entry.type = tarfile.SYMTYPE; entry.linkname = link
                    archive.addfile(entry)
                else:
                    entry.size = len(body); entry.mode = 0o755
                    archive.addfile(entry, io.BytesIO(body))
        stream.seek(0)
        return stream

    def test_rootfs_absolute_links_are_relocated_inside_the_package(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)/'rootfs'
            with tarfile.open(fileobj=self.archive([
                    ('usr/bin/fixture', b'executable', None),
                    ('bin', b'', '/usr/bin')])) as archive:
                package.extract_rootfs(archive, destination)
            self.assertEqual((destination/'bin/fixture').read_bytes(), b'executable')
            self.assertTrue((destination/'bin').resolve().is_relative_to(destination))

    def test_rootfs_archive_cannot_write_outside_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            with tarfile.open(fileobj=self.archive([('../outside', b'bad', None)])) as archive:
                with self.assertRaisesRegex(ValueError, 'path'):
                    package.extract_rootfs(archive, Path(directory)/'rootfs')
            self.assertFalse((Path(directory)/'outside').exists())

    def test_rootfs_link_cannot_escape_to_host_files(self):
        with tempfile.TemporaryDirectory() as directory:
            with tarfile.open(fileobj=self.archive([('escape', b'', '../../outside')])) as archive:
                with self.assertRaisesRegex(ValueError, 'link'):
                    package.extract_rootfs(archive, Path(directory)/'rootfs')

    def test_absolute_link_parent_components_stay_at_guest_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'rootfs'
            with tarfile.open(fileobj=self.archive([('link',b'','/usr/../../outside')])) as archive:
                package.extract_rootfs(archive,root)
            self.assertTrue((root/'link').resolve().is_relative_to(root))

    def test_project_snapshot_excludes_disc_translation_and_user_data(self):
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'source';root.mkdir()
            subprocess.run(['git','init','-q',root],check=True)
            (root/'.gitignore').write_text('disc/\ngenerated/\nlocal/\nbuild/\n')
            (root/'source.cpp').write_text('// authored source\n')
            for name in ('disc/main.dol','generated/game.cpp','local/user/save','build/WiiCompiled'):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('private')
            subprocess.run(['git','-C',root,'add','.'],check=True)
            subprocess.run(['git','-C',root,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid',
                            'commit','-qm','Fixture'],check=True)
            target=Path(directory)/'package'
            package.project_snapshot(root,target)
            self.assertTrue((target/'source.cpp').is_file())
            for name in ('disc','generated','local','build'):
                self.assertFalse((target/name).exists(), name)
            self.assertTrue((target/'.git/shallow').is_file())


if __name__ == '__main__':
    unittest.main()
