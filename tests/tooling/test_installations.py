# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from installer.installations import InstallationStore
import installer.installations as installations


class InstallationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="kirby's installation ")
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.store=InstallationStore(self.root)
        self.dsp=bytes(4096)
        self.dsp_hash=hashlib.sha256(self.dsp).hexdigest()
        constant=patch.object(installations,'DSP_COEF_SHA256',self.dsp_hash)
        constant.start();self.addCleanup(constant.stop)

    def candidate(self,name,body):
        directory=self.root/name;directory.mkdir()
        (directory/'WiiCompiled').write_bytes(body)
        (directory/'dsp_coef.bin').write_bytes(self.dsp)
        manifest=dict(version=1,game_id='RK5E01',revision=0,dol_sha1='f4280a2b0709bfde4132be22e9f7fa8ecfa2a130',
                      data_root=str(self.root/'DATA'),project_sha256='1'*64,translation_key='2'*64,toolchain_image_id='3'*64,
                      executable=dict(file='WiiCompiled',sha256=hashlib.sha256(body).hexdigest(),size=len(body)),
                      verification=['bootstrap','init','disc','es','video'],
                      resources=[dict(file='dsp_coef.bin',sha256=self.dsp_hash,size=4096)])
        return directory,manifest

    def test_publish_and_atomic_selection_keep_the_previous_executable(self):
        first=self.store.publish(*self.candidate('first',b'first executable'))
        second=self.store.publish(*self.candidate('second',b'second executable'))
        self.store.activate(first)
        before=self.store.read_active()
        self.assertEqual(before['current'],first)
        self.store.activate(second)
        active=self.store.read_active()
        self.assertEqual(active,dict(version=1,current=second,previous=first))
        self.assertEqual(self.store.executable(first).read_bytes(),b'first executable')
        self.assertEqual(self.store.executable(second).read_bytes(),b'second executable')

    def test_published_bytes_are_independent_of_the_mutable_compiler_output(self):
        directory,manifest=self.candidate('compiler',b'verified executable')
        identifier=self.store.publish(directory,manifest)
        (directory/'WiiCompiled').write_bytes(b'changed compiler output')
        self.assertEqual(self.store.executable(identifier).read_bytes(),b'verified executable')

    def test_cancel_before_activation_preserves_pointer_and_save(self):
        first=self.store.publish(*self.candidate('first',b'old'))
        second=self.store.publish(*self.candidate('second',b'new'))
        self.store.activate(first)
        saved=self.root/'local/user/nand/save';saved.parent.mkdir(parents=True);saved.write_bytes(b'keep')
        def cancelled():raise InterruptedError('cancelled')
        with self.assertRaises(InterruptedError):self.store.activate(second,check_cancel=cancelled)
        self.assertEqual(self.store.read_active()['current'],first)
        self.assertEqual(saved.read_bytes(),b'keep')

    def test_wrong_digest_and_missing_verification_cannot_be_published(self):
        directory,manifest=self.candidate('bad',b'executable')
        manifest['executable']['sha256']='0'*64
        with self.assertRaises(ValueError):self.store.publish(directory,manifest)
        directory,manifest=self.candidate('unchecked',b'executable')
        manifest['verification'].remove('video')
        with self.assertRaises(ValueError):self.store.publish(directory,manifest)
        self.assertIsNone(self.store.read_active())

    def test_damaged_installation_is_rejected_before_selection(self):
        first=self.store.publish(*self.candidate('first',b'old'))
        second=self.store.publish(*self.candidate('second',b'new'))
        self.store.activate(first)
        self.store.executable(second).chmod(0o700)
        self.store.executable(second).write_bytes(b'broken')
        with self.assertRaises(ValueError):self.store.activate(second)
        self.assertEqual(self.store.read_active()['current'],first)

    def test_rollback_validates_and_switches_to_the_previous_immutable_build(self):
        first=self.store.publish(*self.candidate('first',b'old'))
        second=self.store.publish(*self.candidate('second',b'new'))
        self.store.activate(first);self.store.activate(second)
        self.store.rollback()
        self.assertEqual(self.store.read_active()['current'],first)
        self.assertEqual(self.store.read_active()['previous'],second)

    def test_unowned_paths_and_linked_pointer_are_rejected(self):
        with self.assertRaises(ValueError):self.store.executable('../elsewhere')
        directory,manifest=self.candidate('candidate',b'executable')
        identifier=self.store.publish(directory,manifest)
        target=self.root/'keep.json';target.write_text('{}')
        pointer=self.root/'local/user/installation.json';pointer.parent.mkdir(parents=True,exist_ok=True)
        pointer.symlink_to(target)
        with self.assertRaises(ValueError):self.store.activate(identifier)
        self.assertEqual(target.read_text(),'{}')

    def test_runtime_resources_are_required_hashed_and_copied_with_the_binary(self):
        directory,manifest=self.candidate('bundle',b'executable')
        manifest['resources']=[dict(file='dsp_coef.bin',sha256=self.dsp_hash,size=4096)]
        (directory/'dsp_coef.bin').unlink()
        with patch.object(installations,'DSP_COEF_SHA256',self.dsp_hash,create=True):
            with self.assertRaises((ValueError,OSError)):self.store.publish(directory,manifest)
            (directory/'dsp_coef.bin').write_bytes(self.dsp)
            identifier=self.store.publish(directory,manifest)
            self.assertEqual((self.store.executable(identifier).parent/'dsp_coef.bin').read_bytes(),self.dsp)
