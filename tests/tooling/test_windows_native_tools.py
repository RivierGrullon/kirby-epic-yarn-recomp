# SPDX-License-Identifier: GPL-3.0-only
"""Native Windows worker boundaries, with executable tool fixtures on Linux."""
import contextlib
import hashlib
import io
import importlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from installer.jobs import Cancelled, Job
from installer.pipeline import BuildTools

REPO = Path(__file__).resolve().parents[2]


class WindowsNativeToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kirby-native-tools-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'project with spaces'
        (self.root / 'tools/windows').mkdir(parents=True)
        self.manifest = json.loads((REPO / 'tools/windows/windows-dependencies.json').read_text())
        (self.root / 'tools/windows/windows-dependencies.json').write_text(json.dumps(self.manifest))
        self.local = Path(self.temp.name) / 'appdata'
        self.env = patch.dict(os.environ, {'LOCALAPPDATA': str(self.local)})
        self.env.start(); self.addCleanup(self.env.stop)

    def adapter(self):
        spec = importlib.util.find_spec('installer.windows_tools')
        self.assertIsNotNone(spec, 'The Windows worker needs a native toolchain adapter')
        module = importlib.import_module('installer.windows_tools')
        change = patch.object(module, 'WINDOWS', True)
        change.start(); self.addCleanup(change.stop)
        return module

    def populate(self, cache):
        cache.mkdir(parents=True, exist_ok=True)
        identity = hashlib.sha256(json.dumps(self.manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        (cache / '.kirby-toolchain').write_text(identity + '\n')
        for name, spec in self.manifest.items():
            package = cache / (name + '-' + spec['hash'][:16])
            entry = package / spec['entry']
            entry.parent.mkdir(parents=True, exist_ok=True)
            entry.write_bytes(b'fixture tool')
            (package / '.complete').write_text(spec['hash'] + '\n')
        python = cache / ('python-' + self.manifest['python']['hash'][:16]) / 'python.exe'
        shutil.copy2(sys.executable, python)
        if shutil.which('git'):
            git = cache / ('git-' + self.manifest['git']['hash'][:16]) / 'cmd/git.exe'
            shutil.copy2(shutil.which('git'), git)
        return cache

    def prepared(self):
        module = self.adapter()
        cache = self.populate(module.WindowsTools.cache_path(self.root))
        return module, module.WindowsTools.find(self.root), cache

    def test_pins_define_stable_identity_and_shared_cache_independent_of_project_location(self):
        module, tools, cache = self.prepared()
        self.assertIsNotNone(tools)
        self.assertRegex(tools.image_id, r'^[0-9a-f]{64}$')
        self.assertEqual(cache, self.local / 'KirbyNative/toolchains/windows' / tools.image_id[:16])
        self.assertEqual((cache / '.kirby-toolchain').read_text().strip(), tools.image_id)
        other = Path(self.temp.name) / 'other checkout'
        (other / 'tools/windows').mkdir(parents=True)
        (other / 'tools/windows/windows-dependencies.json').write_text(json.dumps(self.manifest, indent=2))
        self.assertEqual(module.WindowsTools.find(other).image_id, tools.image_id)
        self.manifest['ninja']['hash'] = '1' * 64
        (other / 'tools/windows/windows-dependencies.json').write_text(json.dumps(self.manifest))
        self.assertNotEqual(module.WindowsTools.cache_path(other), cache)

    def test_incomplete_cache_keeps_developer_fallback_and_mismatched_pin_is_rejected(self):
        module = self.adapter()
        self.assertIsNone(module.WindowsTools.find(self.root))
        cache = self.populate(module.WindowsTools.cache_path(self.root))
        marker = cache / ('ninja-' + self.manifest['ninja']['hash'][:16]) / '.complete'
        marker.write_text('0' * 64)
        with self.assertRaisesRegex(ValueError, 'pin|checksum|identity'):
            module.WindowsTools.find(self.root)

    def test_manifest_rejects_unsafe_paths_and_unpinned_downloads_before_preparing(self):
        module = self.adapter()
        for key, value in (('entry', '../outside.exe'), ('hash', '123'), ('url', 'http://example.com/a.zip')):
            with self.subTest(key=key):
                bad = json.loads(json.dumps(self.manifest)); bad['python'][key] = value
                (self.root / 'tools/windows/windows-dependencies.json').write_text(json.dumps(bad))
                with self.assertRaises(ValueError):
                    module.WindowsTools.cache_path(self.root)

    def test_private_environment_carries_sdk_tools_and_removes_frozen_python_state(self):
        _, tools, cache = self.prepared()
        env = tools.environment({'PATH': '/host/tools', 'PYTHONHOME': '/frozen', 'PYTHONPATH': '/wrong',
                                 'DOTNET_ROOT': '/host/sdk', 'KIRBY_TRANSLATOR_DLL': '/wrong.dll'})
        self.assertNotIn('PYTHONHOME', env)
        self.assertNotIn('PYTHONPATH', env)
        self.assertNotIn('KIRBY_TRANSLATOR_DLL', env)
        self.assertEqual(env['DOTNET_ROOT'], str(cache / ('dotnet-' + self.manifest['dotnet']['hash'][:16])))
        self.assertEqual(env['DOTNET_MULTILEVEL_LOOKUP'], '0')
        self.assertEqual(env['KIRBY_PROJECT_ROOT'], str(self.root))
        for name in ('python', 'git', 'cmake', 'ninja', 'mingw', 'dolphin', 'sevenzip'):
            entry = cache / (name + '-' + self.manifest[name]['hash'][:16]) / self.manifest[name]['entry']
            self.assertIn(str(entry.parent), env['PATH'].split(os.pathsep))
        self.assertEqual(tools.command(['7z', 'l'])[0], str(cache / ('sevenzip-' + self.manifest['sevenzip']['hash'][:16]) / '7zr.exe'))
        self.assertEqual(tools.command(['dotnet', '--version'])[0], str(cache / ('dotnet-' + self.manifest['dotnet']['hash'][:16]) / 'dotnet.exe'))

    @unittest.skipIf(os.name == 'nt', 'Executable fixtures use POSIX shebangs')
    def test_python_commands_run_checkout_modules_with_literal_arguments_and_cwd(self):
        _, tools, _ = self.prepared()
        (self.root / 'tools/snapshot_module.py').write_text('VALUE = "checkout module"\n')
        (self.root / 'tools/probe.py').write_text('import json,sys,snapshot_module; print(json.dumps([snapshot_module.VALUE,sys.argv[1:]]))')
        arguments = ['space name', '$(do-not-run)', 'semi;colon']
        with Job(self.root / 'local/setup', 'script', lambda e: None) as job:
            build = BuildTools(job, self.root)
            self.assertEqual(build.image(), tools.image_id)
            result = build.run_tool(['python3', 'tools/probe.py', *arguments], capture_limit=4096)
            self.assertEqual(json.loads(result), ['checkout module', arguments])
            inline = build.run_tool(['python3', '-c', 'import snapshot_module;print(snapshot_module.VALUE)'], capture_limit=4096)
            self.assertEqual(inline.strip(), b'checkout module')
        self.assertFalse(list((self.root / 'local/setup/staging').glob('container-*')))

    @unittest.skipIf(os.name == 'nt', 'Executable fixtures use POSIX shebangs')
    def test_buildtools_uses_private_python_for_bootstrap_identity_and_windows_compilation(self):
        _, tools, _ = self.prepared()
        dependencies = {'wiicompiled': {'commit': 'a' * 40, 'submodules': {'aurora-main': 'b' * 40}}}
        (self.root / 'dependencies.json').write_text(json.dumps(dependencies))
        shutil.copy2(REPO / 'tools/build_info.py', self.root / 'tools/build_info.py')
        (self.root / 'tools/kirby.py').write_text('EXPECTED_SHA1 = "f4280a2b0709bfde4132be22e9f7fa8ecfa2a130"\n')
        (self.root / 'tools/bootstrap.py').write_text('from pathlib import Path;Path("local/bootstrapped").write_text("ready")')
        (self.root / 'tools/build_native.py').write_text('import json,sys;from pathlib import Path;Path("local/native-args.json").write_text(json.dumps(sys.argv[1:]))')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), 'add', 'tools', 'dependencies.json'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.com', 'commit', '-qm', 'fixture'], check=True)
        with patch('installer.pipeline.WINDOWS', True), Job(self.root / 'local/setup', 'build', lambda e: None) as job:
            build = BuildTools(job, self.root)
            build.bootstrap()
            self.assertEqual((self.root / 'local/bootstrapped').read_text(), 'ready')
            self.assertEqual(build.image(), tools.image_id)
            identity = build.identity()
            self.assertRegex(identity['source_sha256'], r'^[0-9a-f]{64}$')
            build.run_tool(['python3', *build.game_command('local/setup/builds/test', 2)])
        arguments = json.loads((self.root / 'local/native-args.json').read_text())
        self.assertEqual(arguments[arguments.index('--llvm-mingw-root') + 1], str(tools.mingw_root))
        self.assertEqual(arguments[arguments.index('--cppwinrt-include') + 1], str(tools.cppwinrt_include))

    def test_run_refuses_a_changed_toolchain_identity_before_starting_a_process(self):
        _, _, _ = self.prepared()
        with Job(self.root / 'local/setup', 'wrong', lambda e: None) as job:
            build = BuildTools(job, self.root)
            build.image_id = 'f' * 64
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                build.run_tool(['python3', '-c', 'raise Exception("must not run")'])

    @unittest.skipIf(os.name == 'nt', 'Executable fixtures use POSIX shebangs')
    def test_prepare_runs_cancellable_powershell_with_explicit_shared_cache(self):
        module = self.adapter()
        powershell = Path(self.temp.name) / 'powershell.exe'
        powershell.write_text('#!' + sys.executable + '\n' + '''import json,sys,pathlib
args=sys.argv[1:]
root=pathlib.Path(args[args.index('-File')+1]).parents[2]
cache=pathlib.Path(args[args.index('-CacheRoot')+1])
(cache/'downloads').mkdir(parents=True,exist_ok=True)
(cache/'invocation.json').write_text(json.dumps(args))
for name,spec in json.loads((root/'tools/windows/windows-dependencies.json').read_text()).items():
    package=cache/(name+'-'+spec['hash'][:16]);entry=package/spec['entry']
    entry.parent.mkdir(parents=True,exist_ok=True);entry.write_bytes(b'fixture')
    (package/'.complete').write_text(spec['hash'])
''')
        powershell.chmod(0o700)
        with patch('installer.windows_tools.shutil.which', return_value=str(powershell)):
            with Job(self.root / 'local/setup', 'prepare', lambda e: None) as job:
                tools = module.WindowsTools.prepare(self.root, job)
            arguments = json.loads((tools.base / 'invocation.json').read_text())
            self.assertIn('-ToolsOnly', arguments)
            self.assertEqual(arguments[arguments.index('-CacheRoot') + 1], str(tools.base))
            with Job(self.root / 'local/setup', 'cancelled', lambda e: None) as job:
                job.cancel.set()
                with self.assertRaises(Cancelled):
                    module.WindowsTools.prepare(self.root, job)

    def extraction_fixture(self, tools):
        from tooling.test_install_sources import make_data
        data = self.root / 'fixture/DATA'
        digest = make_data(data)
        dolphin = tools.entries['dolphin']
        dolphin.write_text('#!' + sys.executable + '\nimport pathlib,shutil,sys\n'
                           'out=pathlib.Path(sys.argv[sys.argv.index("-o")+1])\n'
                           f'shutil.copytree({str(data)!r},out/"DATA")\n')
        dolphin.chmod(0o700)
        archive = tools.entries['sevenzip']
        archive.write_text('#!' + sys.executable + '\nimport pathlib,sys\n'
                           'if sys.argv[1] == "l": print("Path = disc.wbfs\\nSize = 5\\n")\n'
                           'else:\n'
                           '    out=next(arg[2:] for arg in sys.argv if arg.startswith("-o"))\n'
                           '    (pathlib.Path(out)/"disc.wbfs").write_bytes(b"image")\n')
        archive.chmod(0o700)
        return digest

    @unittest.skipIf(os.name == 'nt', 'Executable fixtures use POSIX shebangs')
    def test_archive_inspection_uses_pinned_7zr_and_dolphin_without_host_path(self):
        import kirby
        from installer.sources import inspect_source
        _, tools, _ = self.prepared()
        digest = self.extraction_fixture(tools)
        source = self.root / 'own disc.7z'; source.write_bytes(b'archive fixture')
        with (patch('installer.sources.ROOT', self.root), patch('installer.sources.WINDOWS', True),
              patch.object(kirby, 'EXPECTED_SHA1', digest),
              Job(self.root / 'local/setup', 'archive', lambda e: None) as job):
            result = inspect_source(source, job)
        self.assertEqual(result['dol_sha1'], digest)
        self.assertEqual(result['source_kind'], '7z')

    @unittest.skipIf(os.name == 'nt', 'Executable fixtures use POSIX shebangs')
    def test_worker_prepares_private_environment_before_inspecting_image(self):
        import install as worker
        import kirby
        _, tools, _ = self.prepared()
        digest = self.extraction_fixture(tools)
        powershell = Path(self.temp.name) / 'powershell.exe'
        powershell.write_text('#!' + sys.executable + '\n'); powershell.chmod(0o700)
        source = self.root / 'own disc.wbfs'; source.write_bytes(b'image fixture')
        output = io.StringIO()
        real_inspect = worker.inspect_source
        observed = []
        def inspect(selected, job):
            observed.append(os.environ.get('DOTNET_ROOT'))
            self.assertEqual(observed[-1], str(tools.entries['dotnet'].parent))
            return real_inspect(selected, job)
        with (patch.dict(os.environ), patch('installer.windows_tools.shutil.which', return_value=str(powershell)),
              patch.object(worker, 'ROOT', self.root), patch.object(worker, 'WINDOWS', True),
              patch.object(worker, 'RequestReader'), patch.object(worker.signal, 'signal'),
              patch.object(worker, 'inspect_source', side_effect=inspect),
              patch('installer.sources.ROOT', self.root), patch('installer.sources.WINDOWS', True),
              patch.object(kirby, 'EXPECTED_SHA1', digest),
              patch.object(sys, 'argv', ['install', '--inspect', str(source), '--workspace', str(self.root/'local/setup')]),
              contextlib.redirect_stdout(output)):
            self.assertEqual(worker.main(), 0, output.getvalue())
        self.assertEqual(observed, [str(tools.entries['dotnet'].parent)])
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(events[-1]['state'], 'complete')

    def test_cache_rejects_a_different_full_identity_in_the_short_directory(self):
        module, tools, cache = self.prepared()
        (cache / '.kirby-toolchain').write_text('f' * 64)
        with self.assertRaisesRegex(ValueError, 'identity'):
            module.WindowsTools.find(self.root)
        with Job(self.root / 'local/setup', 'collision', lambda e: None) as job:
            with self.assertRaisesRegex(ValueError, 'identity'):
                module.WindowsTools.prepare(self.root, job)
        self.assertEqual((cache / '.kirby-toolchain').read_text(), 'f' * 64)

    def test_frozen_windows_worker_resets_inherited_dll_directory_before_tools(self):
        module = self.adapter()
        with patch.object(sys, 'frozen', True, create=True), patch('ctypes.WinDLL', create=True) as dll:
            dll.return_value.SetDllDirectoryW.return_value = 1
            module.reset_frozen_dll_directory()
        dll.return_value.SetDllDirectoryW.assert_called_once_with(None)

    @unittest.skipIf(os.name == 'nt', 'Uses the host POSIX Python standard library')
    def test_shared_embedded_interpreter_selects_each_snapshot_from_environment(self):
        module, first, _ = self.prepared()
        customize = REPO / 'tools/windows/sitecustomize.py'
        self.assertTrue(customize.is_file(), 'Shared Python needs environment-selected checkout imports')
        python_home = first.entries['python'].parent
        shutil.copy2(customize, python_home / 'sitecustomize.py')
        stdlib = sysconfig.get_paths()['stdlib']
        (python_home / 'python.exe._pth').write_text('.\n' + stdlib + '\n' + str(Path(stdlib)/'lib-dynload') + '\nimport site\n')
        other = Path(self.temp.name) / 'second snapshot'
        (other / 'tools/windows').mkdir(parents=True)
        (other / 'tools/windows/windows-dependencies.json').write_text(json.dumps(self.manifest))
        second = module.WindowsTools.find(other)
        self.assertEqual(first.base, second.base)
        for tools, value in ((first, 'first snapshot'), (second, 'second snapshot'), (first, 'first snapshot')):
            (tools.root / 'tools/snapshot_module.py').write_text('VALUE = ' + repr(value) + '\n')
            result = subprocess.run([str(tools.entries['python']), '-c',
                                     'import snapshot_module;print(snapshot_module.VALUE)'],
                                    cwd=tools.root, env=tools.environment(), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), value)

    def test_non_windows_host_keeps_existing_container_selection(self):
        module, _, _ = self.prepared()
        with patch.object(module, 'WINDOWS', False):
            self.assertIsNone(module.WindowsTools.find(self.root))
