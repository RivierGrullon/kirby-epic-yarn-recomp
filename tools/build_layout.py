# SPDX-License-Identifier: GPL-3.0-only
"""Output locations for developer builds and owned, isolated setup stages."""
import json
import os
from pathlib import Path
import re
import uuid

from installer.platforms import is_link, open_read


class BuildLayout:
    def __init__(self, root, stage_root=None):
        self.root=Path(root).resolve()
        self.stage=Path(os.path.abspath(stage_root)) if stage_root is not None else None
        if self.stage is None:
            self.generated=self.root/'generated'
            self.metadata=self.root/'local'
            self.logs=self.root/'build-logs'
            self.workspace=self.root/'local/native-workspace'
            self.build=self.root/'build/native'
            self.translator_artifacts=None
        else:
            self._check_stage()
            self.generated=self.stage/'generated'
            self.metadata=self.stage/'metadata'
            self.logs=self.stage/'logs'
            self.workspace=self.stage/'native-workspace'
            self.build=self.stage/'build'
            self.translator_artifacts=self.stage/'translator'

    def _check_stage(self):
        # Check the supplied path before resolving: resolve() would hide a
        # symlink or junction. Then use the same spelling as the resolved root,
        # including expansion of Windows 8.3 aliases used by runner TEMP paths.
        for path in (self.stage,*self.stage.parents):
            if path==self.root:
                break
            if is_link(path):
                raise ValueError('Build stage paths must not contain symlinks')
        self.stage=self.stage.resolve()
        base=self.root/'local/setup/builds'
        if self.stage==base or not self.stage.is_relative_to(base):
            raise ValueError('Stage root must be a directory below local/setup/builds/')
        relative=self.stage.relative_to(base)
        if len(relative.parts)!=1 or not re.fullmatch(r'[A-Za-z0-9_-]{1,80}',relative.name):
            raise ValueError('Build job ID must contain 1-80 ASCII letters, digits, underscores or hyphens')

    def ensure(self):
        if self.stage is not None:
            self._check_stage()
            self.stage.mkdir(parents=True,exist_ok=True,mode=0o700)
            marker=self.stage/'.layout.json'
            previous=None
            if marker.exists() or is_link(marker):
                if is_link(marker) or not marker.is_file() or marker.stat().st_size>4096:
                    raise ValueError('Invalid build stage ownership record')
                with os.fdopen(open_read(marker),'r',encoding='utf-8') as stream:
                    previous=json.load(stream)
                if not isinstance(previous,dict):
                    raise ValueError('Invalid build stage ownership record')
                if previous.get('version')==1:
                    info=self.root.stat()
                    legacy=dict(version=1,project_identity=dict(device=info.st_dev,inode=info.st_ino),
                                kind='kirby-native-build-stage')
                    if previous!=legacy:
                        raise ValueError('Build stage belongs to another layout or checkout')
            elif any(self.stage.iterdir()):
                raise ValueError('Refusing an unowned, nonempty build stage')
            # A private checkout token survives Windows-to-Linux bind mounts,
            # whose device/inode identities are not shared with the host.
            token_path=self.root/'local/setup/project-id'
            try:
                fd=os.open(token_path,os.O_CREAT|os.O_EXCL|os.O_WRONLY|getattr(os,'O_NOFOLLOW',0),0o600)
            except FileExistsError:
                pass
            else:
                with os.fdopen(fd,'w',encoding='ascii') as stream:
                    stream.write(uuid.uuid4().hex+'\n');stream.flush();os.fsync(stream.fileno())
            with os.fdopen(open_read(token_path),'r',encoding='ascii') as stream:
                token=stream.read(34).strip()
            if not re.fullmatch('[a-f0-9]{32}',token):
                raise ValueError('Invalid checkout ownership token')
            record=dict(version=2,project_identity=dict(token=token),kind='kirby-native-build-stage')
            if previous is not None and previous.get('version')!=1 and previous!=record:
                raise ValueError('Build stage belongs to another layout or checkout')
            if previous!=record:
                import tempfile
                fd,name=tempfile.mkstemp(prefix='.layout-',dir=self.stage)
                try:
                    with os.fdopen(fd,'w',encoding='utf-8') as stream:
                        stream.write(json.dumps(record)+'\n');stream.flush();os.fsync(stream.fileno())
                    os.replace(name,marker)
                finally:
                    Path(name).unlink(missing_ok=True)
            for path in (self.generated,self.metadata,self.logs,self.workspace,self.build,self.translator_artifacts):
                if is_link(path):
                    raise ValueError('Build output directories must not be symlinks')
        for path in (self.generated,self.metadata,self.logs):
            path.mkdir(parents=True,exist_ok=True,mode=0o700)
