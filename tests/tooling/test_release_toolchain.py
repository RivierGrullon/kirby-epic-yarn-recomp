# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import release_toolchain as release

ROOT = Path(__file__).resolve().parents[2]


class ReleaseToolchainTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in (*release.INPUT_FILES, 'tools/package_portable.py', 'dependencies.json'):
            destination = self.root/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT/name, destination)
        self.record = dict(version=1, platform='linux-x86_64', image_id='a'*64,
                           url='https://example.invalid/tools.tar.xz', sha256='b'*64,
                           size=100, unpacked_size=200,
                           translator_revision=release.translator_revision(self.root))
        self.body = json.dumps(self.record).encode()
        self.pin = dict(version=1, descriptor_url=self.record['url']+'.json',
                        descriptor_sha256=hashlib.sha256(self.body).hexdigest(),
                        inputs_sha256=release.fingerprint(self.root))

    def test_fingerprint_tracks_tool_recipe_and_translator_but_ignores_setup(self):
        original = release.fingerprint(self.root)
        path = self.root/'tools/package_portable.py'
        path.write_text(path.read_text().replace("print('AppDir ready:',output)", "print('Installer ready:',output)"))
        (self.root/'tools/setup_gui.py').write_text('# GUI-only change\n')
        self.assertEqual(release.fingerprint(self.root), original)
        path.write_text(path.read_text().replace("identifier='kirby-package-'", "identifier='new-export-'"))
        self.assertNotEqual(release.fingerprint(self.root), original)
        shutil.copy2(ROOT/'tools/package_portable.py', path)
        recipe = self.root/release.INPUT_FILES[0]
        recipe.write_text(recipe.read_text()+'\n# updated recipe\n')
        self.assertNotEqual(release.fingerprint(self.root), original)
        shutil.copy2(ROOT/release.INPUT_FILES[0], recipe)
        dependencies = json.loads((self.root/'dependencies.json').read_text())
        dependencies['wiicompiled']['commit'] = 'c'*40
        (self.root/'dependencies.json').write_text(json.dumps(dependencies))
        self.assertNotEqual(release.fingerprint(self.root), original)

    def test_fingerprint_has_same_identity_across_python_versions(self):
        for name in release.INPUT_FILES:
            (self.root/name).write_text('fixture: '+name+'\n')
        source = ''.join(f'def {name}(value):\n    return value\n\n'
                         for name in release.EXPORT_FUNCTIONS)
        (self.root/'tools/package_portable.py').write_text(source)
        (self.root/'dependencies.json').write_text(json.dumps({'wiicompiled': {'commit': 'b'*40}}))
        # This fixed identity must pass on both the local interpreter and CI's Python 3.12.
        self.assertEqual(release.fingerprint(self.root), '0c8e97cb50a360150ecfae3813253a12ef04abb65dbd79e1b826eb0b8ef468a8')

    def test_resolve_writes_only_verified_matching_descriptor(self):
        output = self.root/'out/tools.json'
        release.resolve(self.root, self.pin, output, fetch=lambda url: self.body)
        self.assertEqual(output.read_bytes(), self.body)

    def test_resolve_rejects_stale_inputs_before_network(self):
        self.pin['inputs_sha256'] = '0'*64
        def unexpected_fetch(url):
            self.fail('Stale tools must fail before network access')
        with self.assertRaisesRegex(ValueError, 'tools inputs changed'):
            release.resolve(self.root, self.pin, self.root/'out.json', fetch=unexpected_fetch)

    def test_resolve_rejects_corrupt_descriptor_and_wrong_translator(self):
        output = self.root/'out.json'
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            release.resolve(self.root, self.pin, output, fetch=lambda url: self.body+b' ')
        self.assertFalse(output.exists())
        self.record['translator_revision'] = 'c'*40
        body = json.dumps(self.record).encode()
        self.pin['descriptor_sha256'] = hashlib.sha256(body).hexdigest()
        with self.assertRaisesRegex(ValueError, 'translator'):
            release.resolve(self.root, self.pin, output, fetch=lambda url: body)
        self.assertFalse(output.exists())

    def test_rejects_invalid_pin_url_or_oversized_descriptor(self):
        for url in ('http://example.invalid/tools.json', 'https://user:password@example.invalid/a',
                    'https://example.invalid/a#fragment'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                release.resolve(self.root, dict(self.pin, descriptor_url=url), self.root/'out.json',
                                fetch=lambda url: self.body)
        with self.assertRaisesRegex(ValueError, 'large'):
            release.resolve(self.root, self.pin, self.root/'out.json', fetch=lambda url: b'x'*8193)

    def test_generate_pin_checks_archive_bytes_and_translator(self):
        archive = self.root/release.ARCHIVE
        archive.write_bytes(b'real archive content')
        self.record.update(size=archive.stat().st_size, sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
        descriptor = archive.with_name(archive.name+'.json')
        descriptor.write_text(json.dumps(self.record))
        pin = release.make_pin(self.root, archive, self.record['url']+'.json')
        self.assertEqual(pin['inputs_sha256'], release.fingerprint(self.root))
        self.assertEqual(pin['descriptor_sha256'], hashlib.sha256(descriptor.read_bytes()).hexdigest())
        wrong_revision = dict(self.record, translator_revision='c'*40)
        descriptor.write_text(json.dumps(wrong_revision))
        with self.assertRaisesRegex(ValueError, 'translator'):
            release.make_pin(self.root, archive, self.record['url']+'.json')
        descriptor.write_text(json.dumps(self.record))
        with self.assertRaisesRegex(ValueError, 'URL'):
            release.make_pin(self.root, archive, 'https://example.invalid/other.json')
        archive.write_bytes(b'tampered content')
        with self.assertRaisesRegex(ValueError, 'archive'):
            release.make_pin(self.root, archive, self.record['url']+'.json')

    def test_release_url_accepts_game_tags_and_rejects_unsafe_targets(self):
        self.assertEqual(release.archive_url('owner/repo', 'v0.2.0-alpha.1'),
                         'https://github.com/owner/repo/releases/download/v0.2.0-alpha.1/'+release.ARCHIVE)
        for tag in ('../v1', 'v1/extra', 'v1-$(id)', 'v1 a', '', 'v1.', 'v1.lock', 'tools-linux-test'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.archive_url('owner/repo', tag)
        with self.assertRaises(ValueError):
            release.archive_url('../repo', 'v0.2.0')

    def test_versioned_tools_can_share_a_game_release_without_replacing_old_downloads(self):
        self.assertEqual(release.archive_url('owner/repo', 'v0.2.0', asset_id='12345'),
                         'https://github.com/owner/repo/releases/download/v0.2.0/Kirby-tools-linux-x86_64-12345.tar.xz')
        record = self.game_release()
        record['assets'].append({'name': release.ARCHIVE})
        release.validate_release(record, 'v0.2.0', asset_id='12345')
        record['assets'].append({'name': 'Kirby-tools-linux-x86_64-12345.tar.xz.json'})
        with self.assertRaisesRegex(ValueError, 'already contains'):
            release.validate_release(record, 'v0.2.0', asset_id='12345')

    def test_versioned_tools_reject_unsafe_asset_identifiers(self):
        for identifier in ('../1', 'x/y', '$(id)', '1 2', ''):
            with self.subTest(identifier=identifier), self.assertRaises(ValueError):
                release.archive_url('owner/repo', 'v0.2.0', asset_id=identifier)

    def game_release(self, **changes):
        return dict(dict(tag_name='v0.2.0', immutable=False, draft=False,
                         assets=[{'name': 'Kirby-Setup-linux-x86_64.AppImage'}]), **changes)

    def test_target_release_accepts_existing_game_installer(self):
        for name in ('Kirby-Setup-linux-x86_64.AppImage', 'Kirby-Setup-windows-x86_64.tar.gz'):
            with self.subTest(name=name):
                release.validate_release(self.game_release(assets=[{'name': name}]), 'v0.2.0')

    def test_target_release_accepts_versioned_game_installer(self):
        for name in ('Kirby-Setup-0.2.0-linux-x86_64.AppImage', 'Kirby-Setup-0.2.0-windows-x86_64.zip'):
            with self.subTest(name=name):
                release.validate_release(self.game_release(assets=[{'name': name}]), 'v0.2.0')

    def test_target_release_rejects_wrong_tag_or_missing_installer(self):
        for record in (self.game_release(tag_name='v0.1.0'), self.game_release(assets=[]),
                       self.game_release(assets=[{'name': 'source.zip'}])):
            with self.subTest(record=record), self.assertRaises(ValueError):
                release.validate_release(record, 'v0.2.0')

    def test_target_release_rejects_immutable_release(self):
        with self.assertRaisesRegex(ValueError, 'immutable'):
            release.validate_release(self.game_release(immutable=True), 'v0.2.0')

    def test_target_release_never_replaces_any_existing_tools_asset(self):
        for name in (release.ARCHIVE, release.ARCHIVE+'.sha256', release.ARCHIVE+'.json',
                     release.ARCHIVE+'.json.sha256'):
            record = self.game_release()
            record['assets'].append({'name': name})
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'already contains'):
                release.validate_release(record, 'v0.2.0')


if __name__ == '__main__':
    unittest.main()
