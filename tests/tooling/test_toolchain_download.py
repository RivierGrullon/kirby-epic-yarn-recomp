# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import contextlib
from concurrent.futures import ThreadPoolExecutor
import io
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from installer.jobs import Job, Cancelled
from installer.portable import PortableTools
from installer import toolchain_download as download
import install as worker


class Response(io.BytesIO):
    def geturl(self):
        return 'https://example.invalid/tools.tar.xz'


class ToolchainDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kirby tools with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base/'version-one'
        self.runtime = self.root/'tools/runtime'
        self.runtime.mkdir(parents=True)
        self.events = []
        env = patch.dict(os.environ, {'XDG_DATA_HOME': str(self.base/'data')})
        env.start(); self.addCleanup(env.stop)
        self.members = [(name, b'fixture tool', None) for name in PortableTools.REQUIRED]
        manifest = dict(version=1, platform='linux-x86_64', image_id='a'*64,
                        files={name: hashlib.sha256(body).hexdigest() for name, body, _ in self.members})
        self.members.append(('toolchain.json', json.dumps(manifest).encode(), None))

    def bundle(self, extras=()):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode='w:xz') as archive:
            for name, body, link in [*self.members, *extras]:
                entry = tarfile.TarInfo(name)
                entry.mode = 0o755
                if link is not None:
                    entry.type = tarfile.SYMTYPE; entry.linkname = link
                else:
                    entry.size = len(body)
                archive.addfile(entry, io.BytesIO(body) if link is None else None)
        self.payload = stream.getvalue()
        self.record = dict(version=1, platform='linux-x86_64', image_id='a'*64,
                           url='https://example.invalid/tools.tar.xz',
                           sha256=hashlib.sha256(self.payload).hexdigest(), size=len(self.payload),
                           unpacked_size=sum(len(body) for _, body, link in [*self.members, *extras] if link is None))
        self.descriptor()

    def descriptor(self):
        (self.runtime/'toolchain-download.json').write_text(json.dumps(self.record))

    def job(self, name='download'):
        return Job(self.root/'local/setup', name, self.events.append)

    def test_tools_download_once_and_are_reused_offline_by_another_version(self):
        self.bundle()
        with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
            with self.job() as job:
                tools = download.ensure_toolchain(self.root, job)
        self.assertEqual((tools.base/'proot').read_bytes(), b'fixture tool')
        self.assertEqual(tools.base, self.base/'data/kirby-native/toolchains'/self.record['sha256'])
        second = self.base/'version-two'
        (second/'tools/runtime').mkdir(parents=True)
        (second/'tools/runtime/toolchain-download.json').write_text(json.dumps(self.record))
        with patch.object(download.urllib.request, 'urlopen', side_effect=AssertionError('Must work offline')):
            with self.job('reuse') as job:
                reused = download.ensure_toolchain(second, job)
        self.assertEqual(reused.base, tools.base)
        self.assertEqual(PortableTools.find(second).base, tools.base)
        self.assertEqual(list(tools.base.parent.glob('.download-*')), [])

    def test_incorrect_hash_and_truncated_download_leave_no_selected_tools(self):
        self.bundle()
        for name, payload in [('hash', b'x'*len(self.payload)), ('truncated', self.payload[:-4])]:
            with self.subTest(name=name), patch.object(download.urllib.request, 'urlopen', return_value=Response(payload)):
                with self.job(name) as job, self.assertRaisesRegex(ValueError, 'checksum|size'):
                    download.ensure_toolchain(self.root, job)
            with self.assertRaisesRegex(ValueError, 'download|installed'):
                PortableTools.find(self.root)
            self.assertFalse((self.base/'data/kirby-native/toolchains'/self.record['sha256']).exists())

    def test_cancelled_transfer_does_not_publish_cache_and_can_retry(self):
        self.bundle()
        with self.job('cancelled') as job:
            class CancelResponse(Response):
                def read(inner, count=-1):
                    result = super().read(count)
                    job.cancel.set()
                    return result
            with patch.object(download.urllib.request, 'urlopen', return_value=CancelResponse(self.payload)):
                with self.assertRaises(Cancelled):
                    download.ensure_toolchain(self.root, job)
        self.assertFalse((self.base/'data/kirby-native/toolchains'/self.record['sha256']).exists())
        with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
            with self.job('retry') as job:
                self.assertIsNotNone(download.ensure_toolchain(self.root, job))

    def test_archive_paths_links_and_special_files_cannot_escape_cache(self):
        cases = [('../escaped', b'bad', None), ('rootfs/escape', b'', '../../escaped'),
                 ('rootfs/escape', b'', '/etc/passwd')]
        for index, extra in enumerate(cases):
            with self.subTest(extra=extra):
                self.bundle([extra])
                with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
                    with self.job('unsafe-'+str(index)) as job, self.assertRaisesRegex(ValueError, 'path|link'):
                        download.ensure_toolchain(self.root, job)
                self.assertFalse((self.base/'data/kirby-native/escaped').exists())

    def test_internal_relative_symlinks_survive_extraction(self):
        self.bundle([('rootfs/bin', b'', 'usr/bin')])
        with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
            with self.job() as job:
                tools = download.ensure_toolchain(self.root, job)
        self.assertEqual((tools.base/'rootfs/bin/git').read_bytes(), b'fixture tool')

    def test_oversized_archive_expansion_and_wrong_image_identity_are_rejected(self):
        for field, value in [('unpacked_size', 1), ('image_id', 'b'*64)]:
            with self.subTest(field=field):
                self.bundle(); self.record[field] = value; self.descriptor()
                with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
                    with self.job(field) as job, self.assertRaisesRegex(ValueError, 'size|identity'):
                        download.ensure_toolchain(self.root, job)

    def test_descriptor_rejects_insecure_url_and_symlinked_cache(self):
        self.bundle(); self.record['url'] = 'http://example.invalid/tools.tar.xz'; self.descriptor()
        with self.job('http') as job, self.assertRaisesRegex(ValueError, 'HTTPS'):
            download.ensure_toolchain(self.root, job)
        self.bundle()
        cache = self.base/'data/kirby-native/toolchains'
        cache.mkdir(parents=True)
        (cache/self.record['sha256']).symlink_to(self.base, target_is_directory=True)
        with self.job('symlink') as job, self.assertRaisesRegex(ValueError, 'symlink'):
            download.ensure_toolchain(self.root, job)

    def test_absent_descriptor_keeps_developer_tools_optional(self):
        with self.job() as job:
            self.assertIsNone(download.ensure_toolchain(self.root, job))

    def test_simultaneous_versions_select_one_complete_shared_toolchain(self):
        self.bundle()
        second = self.base/'version-two'
        (second/'tools/runtime').mkdir(parents=True)
        (second/'tools/runtime/toolchain-download.json').write_text(json.dumps(self.record))
        def install(root):
            with Job(root/'local/setup', 'parallel', lambda event: None) as job:
                return download.ensure_toolchain(root, job).base
        with patch.object(download.urllib.request, 'urlopen', side_effect=lambda *a, **k: Response(self.payload)):
            with ThreadPoolExecutor(max_workers=2) as executor:
                paths = list(executor.map(install, (self.root, second)))
        self.assertEqual(paths[0], paths[1])
        self.assertEqual((paths[0]/'proot').read_bytes(), b'fixture tool')

    def test_worker_downloads_before_disc_inspection_but_data_inspection_stays_offline(self):
        self.bundle()
        selected = self.base/'own.wbfs'; selected.write_bytes(b'owned disc fixture')
        def inspect(path, job):
            self.assertEqual(PortableTools.find(self.root).image_id, 'a'*64)
            return {'verified': True}
        with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)), \
                patch.object(worker, 'inspect_source', side_effect=inspect):
            self.run_worker(selected, 'disc')
        # Removing the descriptor's cached directory would make any lookup fail.
        self.record['sha256'] = 'f'*64; self.descriptor()
        selected = self.base/'DATA'; selected.mkdir()
        with patch.object(download.urllib.request, 'urlopen', side_effect=AssertionError('DATA inspection is offline')), \
                patch.object(worker, 'inspect_source', return_value={'verified': True}):
            self.run_worker(selected, 'data')

    def run_worker(self, selected, identifier):
        output = io.StringIO()
        with tempfile.TemporaryFile(mode='w+') as request:
            with patch.object(worker, 'ROOT', self.root), patch.object(sys, 'stdin', request), \
                    patch.object(sys, 'argv', ['install.py', '--inspect', str(selected),
                                             '--workspace', str(self.root/'local'/identifier)]), \
                    patch.object(worker.signal, 'signal'), contextlib.redirect_stdout(output):
                self.assertEqual(worker.main(), 0, output.getvalue())
        self.assertEqual(json.loads(output.getvalue().splitlines()[-1])['state'], 'complete')

    def test_bundle_without_manifest_and_damaged_cached_tools_fail_closed(self):
        self.bundle()
        self.members = [item for item in self.members if item[0] != 'toolchain.json']
        self.bundle()
        with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
            with self.job('missing') as job, self.assertRaisesRegex(ValueError, 'manifest'):
                download.ensure_toolchain(self.root, job)

    def test_damaged_cached_tools_fail_closed(self):
        self.bundle()
        with patch.object(download.urllib.request, 'urlopen', return_value=Response(self.payload)):
            with self.job('valid') as job:
                tools = download.ensure_toolchain(self.root, job)
        (tools.base/'proot').write_bytes(b'corrupted')
        with self.job('damaged') as job, self.assertRaisesRegex(ValueError, 'checksum'):
            download.ensure_toolchain(self.root, job)


if __name__ == '__main__':
    unittest.main()
