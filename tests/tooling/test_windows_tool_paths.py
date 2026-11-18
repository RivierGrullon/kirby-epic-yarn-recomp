"""Exercise the real PowerShell downloader with a cached, synthetic SDK ZIP."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/windows/build-windows.ps1'
HARNESS = r'''
param($ScriptPath, $Cache, $ManifestPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$Ast = [Management.Automation.Language.Parser]::ParseFile($ScriptPath, [ref]$null, [ref]$null)
$Functions = $Ast.FindAll({ param($Node)
    $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -in @('Invoke-Checked','Get-Package')
}, $true)
foreach ($Function in $Functions) { . ([scriptblock]::Create($Function.Extent.Text)) }
Get-Package 'dotnet'
'''


@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell 5.1')
class WindowsToolPathTests(unittest.TestCase):
    def extract(self, final_length):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            member = 'sdk/8.0.428/Sdks/' + 'x' * 130 + '.targets'
            identifier_length = len('dotnet-') + 16
            cache_length = final_length - identifier_length - len(member) - 2
            cache = root / ('p' * (cache_length - len(str(root)) - 1))
            downloads = cache / 'downloads'
            downloads.mkdir(parents=True)
            archive = downloads / 'fixture.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('dotnet.exe', b'entry fixture')
                z.writestr(member, b'sdk fixture')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            archive.rename(downloads / f'dotnet-{digest[:16]}.zip')
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'dotnet': dict(hash=digest, algorithm='SHA256',
                                format='zip', entry='dotnet.exe', url='https://invalid.example/unused')}))
            harness = root / 'test.ps1'
            harness.write_text(HARNESS)
            # PowerShell 7's module search path is not compatible with 5.1.
            environment = {key: value for key, value in os.environ.items()
                           if key.lower() != 'psmodulepath'}
            result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(harness), str(SCRIPT), str(cache), str(manifest)],
                                    capture_output=True, text=True, errors='replace', timeout=30, env=environment)
            destination = cache / f'dotnet-{digest[:16]}'
            if result.returncode == 0:
                self.assertEqual((destination / member).read_bytes(), b'sdk fixture')
                self.assertTrue((destination / '.complete').is_file())
                self.assertFalse(list(cache.glob('.s-*')))
            return result

    def test_deep_sdk_path_extracts_with_short_staging(self):
        result = self.extract(254)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sdk_member_longer_than_max_path_extracts(self):
        result = self.extract(320)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
