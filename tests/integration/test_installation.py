# SPDX-License-Identifier: GPL-3.0-only
"""Read actual Python store manifests using the compiled native reader."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.installations import InstallationStore,DSP_COEF_SHA256,canonical


@unittest.skipUnless(os.environ.get('KIRBY_INSTALLATION_TEST'),'Set KIRBY_INSTALLATION_TEST to kirby_setup_installation_test')
class InstallationReaderTests(unittest.TestCase):
    def setUp(self):
        self.program=Path(os.environ['KIRBY_INSTALLATION_TEST']).resolve()
        self.temp=tempfile.TemporaryDirectory(prefix="Kirby's árbol 日本 ")
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.store=InstallationStore(self.root)
        candidate=self.root/'candidate';candidate.mkdir()
        self.body=b'synthetic artifact bytes, never executed'
        (candidate/'WiiCompiled').write_bytes(self.body)
        shutil.copyfile(self.program.parent/'dsp_coef.bin',candidate/'dsp_coef.bin')
        self.manifest=dict(version=1,game_id='RK5E01',revision=0,
            dol_sha1='f4280a2b0709bfde4132be22e9f7fa8ecfa2a130',data_root=str(self.root/'own DATA'),
            project_sha256='a'*64,translation_key='b'*64,toolchain_image_id='c'*64,
            executable=dict(file='WiiCompiled',sha256=hashlib.sha256(self.body).hexdigest(),size=len(self.body)),
            verification=['bootstrap','init','disc','es','video'],
            resources=[dict(file='dsp_coef.bin',sha256=DSP_COEF_SHA256,size=4096)])
        self.identifier=self.store.publish(candidate,self.manifest)
        self.store.activate(self.identifier)

    def read(self):
        return subprocess.run([self.program,'--read',self.root],capture_output=True,text=True,timeout=10)

    def test_canonical_unicode_manifest_and_same_size_corruption(self):
        result=self.read();self.assertEqual(result.returncode,0,result.stderr)
        selected=json.loads(result.stdout)
        self.assertEqual(selected['id'],self.identifier)
        self.assertEqual(selected['data_root'],self.manifest['data_root'])
        self.assertEqual(selected['executable'],str(self.store.executable(self.identifier)))
        executable=self.store.executable(self.identifier);executable.chmod(0o700)
        executable.write_bytes(bytes([self.body[0]^1])+self.body[1:])
        result=self.read();self.assertEqual(result.returncode,1,result.stdout)
        self.assertIn('digest mismatch',result.stderr)
        executable.write_bytes(self.body)
        resource=executable.parent/'dsp_coef.bin';resource.chmod(0o600)
        data=resource.read_bytes();resource.write_bytes(bytes([data[0]^1])+data[1:])
        result=self.read();self.assertEqual(result.returncode,1,result.stdout)
        self.assertIn('digest mismatch',result.stderr)

    def test_self_consistent_manifest_with_missing_startup_checks_is_rejected(self):
        self.manifest['verification'].pop()
        identifier=hashlib.sha256(canonical(self.manifest)).hexdigest()
        old=self.store.executable(self.identifier).parent
        new=self.store.executable(identifier).parent;old.rename(new)
        (new/'manifest.json').write_bytes(canonical(self.manifest))
        self.store.pointer.write_text(json.dumps(dict(version=1,current=identifier,previous=None)))
        result=self.read();self.assertEqual(result.returncode,1,result.stdout)
        self.assertIn('incomplete native verification',result.stderr)
