# SPDX-License-Identifier: GPL-3.0-only
"""Verified immutable build copies and an atomic per-user selection pointer."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile

from kirby import EXPECTED_SHA1

VERIFICATION={'bootstrap','init','disc','es','video'}
DSP_COEF_SHA256='d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179'


def digest(value):
    return isinstance(value,str) and re.fullmatch(r'[a-f0-9]{64}',value) is not None


def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()


def read_json(path):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK|os.O_CLOEXEC)
    with os.fdopen(fd,'rb') as stream:
        info=os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size>131072:
            raise ValueError('Installation metadata must be a bounded regular file')
        data=stream.read(131073)
    if len(data)>131072:
        raise ValueError('Installation metadata exceeds 128 KiB')
    return json.loads(data)


def file_digest(path,check_cancel=lambda:None):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK|os.O_CLOEXEC)
    with os.fdopen(fd,'rb') as stream:
        before=os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError('Native executable is not a regular file')
        value=hashlib.sha256()
        while chunk:=stream.read(1024*1024):
            check_cancel();value.update(chunk)
        after=os.fstat(stream.fileno())
        if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
            raise ValueError('Native executable changed while reading')
    return value.hexdigest(),before.st_size


class InstallationStore:
    def __init__(self,root):
        self.root=Path(root).resolve()
        self.builds=self.root/'local/setup/installations'
        self.pointer=self.root/'local/user/installation.json'

    def _directory(self,path):
        current=self.root
        for part in path.relative_to(self.root).parts:
            current/=part
            if current.is_symlink():
                raise ValueError('Installation directories must not be symlinks')
            current.mkdir(exist_ok=True,mode=0o700)

    def executable(self,identifier):
        if not digest(identifier):
            raise ValueError('Invalid installation identifier')
        return self.builds/identifier/'WiiCompiled'

    def _validate(self,manifest):
        try:
            if (not isinstance(manifest,dict) or type(manifest.get('version')) is not int or manifest['version']!=1
                    or manifest['game_id']!='RK5E01' or type(manifest['revision']) is not int or manifest['revision']!=0
                    or manifest['dol_sha1']!=EXPECTED_SHA1 or not isinstance(manifest['data_root'],str)
                    or '\0' in manifest['data_root'] or not Path(manifest['data_root']).is_absolute()
                    or not all(digest(manifest[key]) for key in ('project_sha256','translation_key','toolchain_image_id'))
                    or manifest['executable']['file']!='WiiCompiled' or not digest(manifest['executable']['sha256'])
                    or type(manifest['executable']['size']) is not int or manifest['executable']['size']<=0
                    or not isinstance(manifest['verification'],list) or set(manifest['verification'])!=VERIFICATION
                    or len(manifest['verification'])!=len(VERIFICATION)
                    or manifest['resources']!=[dict(file='dsp_coef.bin',sha256=DSP_COEF_SHA256,size=4096)]):
                raise ValueError('Installation does not identify a fully verified RK5E01 native build')
        except (KeyError,TypeError) as error:
            raise ValueError('Malformed installation manifest') from error

    def manifest(self,identifier,*,check_cancel=lambda:None):
        path=self.executable(identifier)
        if path.parent.is_symlink():
            raise ValueError('Installation build directory must not be a symlink')
        value=read_json(path.parent/'manifest.json');self._validate(value)
        if hashlib.sha256(canonical(value)).hexdigest()!=identifier:
            raise ValueError('Installation manifest identity mismatch')
        actual,size=file_digest(path,check_cancel)
        if actual!=value['executable']['sha256'] or size!=value['executable']['size']:
            raise ValueError('Installed executable digest or size mismatch')
        for resource in value['resources']:
            actual,size=file_digest(path.parent/resource['file'],check_cancel)
            if actual!=resource['sha256'] or size!=resource['size']:
                raise ValueError('Installed runtime resource digest or size mismatch')
        return value

    def publish(self,candidate,manifest,*,check_cancel=lambda:None):
        self._validate(manifest)
        identifier=hashlib.sha256(canonical(manifest)).hexdigest()
        self._directory(self.builds)
        if (self.builds/identifier).exists():
            self.manifest(identifier,check_cancel=check_cancel)
            return identifier
        with tempfile.TemporaryDirectory(prefix='.publish-',dir=self.builds) as temporary:
            directory=Path(temporary)
            source=Path(candidate)/'WiiCompiled'
            fd=os.open(source,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK|os.O_CLOEXEC)
            with os.fdopen(fd,'rb') as stream:
                if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                    raise ValueError('Build candidate must be a regular native executable')
                with (directory/'WiiCompiled').open('xb') as output:
                    while chunk:=stream.read(1024*1024):check_cancel();output.write(chunk)
                    output.flush();os.fsync(output.fileno())
            actual,size=file_digest(directory/'WiiCompiled',check_cancel)
            if actual!=manifest['executable']['sha256'] or size!=manifest['executable']['size']:
                raise ValueError('Candidate executable digest or size mismatch')
            (directory/'WiiCompiled').chmod(0o500)
            for resource in manifest['resources']:
                source=Path(candidate)/resource['file']
                actual,size=file_digest(source,check_cancel)
                if actual!=resource['sha256'] or size!=resource['size']:
                    raise ValueError('Candidate runtime resource digest or size mismatch')
                shutil.copyfile(source,directory/resource['file'],follow_symlinks=False)
                actual,size=file_digest(directory/resource['file'],check_cancel)
                if actual!=resource['sha256'] or size!=resource['size']:
                    raise ValueError('Copied runtime resource digest or size mismatch')
                (directory/resource['file']).chmod(0o400)
                with (directory/resource['file']).open('rb') as stream:os.fsync(stream.fileno())
            with (directory/'manifest.json').open('xb') as stream:
                stream.write(canonical(manifest)+b'\n');stream.flush();os.fsync(stream.fileno())
            check_cancel()
            os.rename(directory,self.builds/identifier)
        return identifier

    def read_active(self):
        if not self.pointer.exists() and not self.pointer.is_symlink():
            return None
        if self.pointer.is_symlink():
            raise ValueError('Installation selection must not be a symlink')
        value=read_json(self.pointer)
        if (not isinstance(value,dict) or type(value.get('version')) is not int or value['version']!=1
                or not digest(value.get('current')) or (value.get('previous') is not None and not digest(value['previous']))):
            raise ValueError('Invalid active installation selection')
        return value

    def activate(self,identifier,*,check_cancel=lambda:None):
        self.manifest(identifier,check_cancel=check_cancel)
        prior=self.read_active()
        if prior and prior['current']==identifier:
            return prior
        value=dict(version=1,current=identifier,previous=prior['current'] if prior else None)
        self._directory(self.pointer.parent)
        fd,name=tempfile.mkstemp(prefix='.installation-',dir=self.pointer.parent)
        try:
            with os.fdopen(fd,'wb') as stream:
                stream.write(canonical(value)+b'\n');stream.flush();os.fsync(stream.fileno())
            check_cancel()  # Selection commits after this last cancellation check.
            os.replace(name,self.pointer)
            parent=os.open(self.pointer.parent,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
            try:os.fsync(parent)
            finally:os.close(parent)
        finally:
            Path(name).unlink(missing_ok=True)
        return value

    def rollback(self,*,check_cancel=lambda:None):
        value=self.read_active()
        if not value or value['previous'] is None:
            raise ValueError('No previous native installation is available')
        return self.activate(value['previous'],check_cancel=check_cancel)
