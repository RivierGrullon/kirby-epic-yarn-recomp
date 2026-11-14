# SPDX-License-Identifier: GPL-3.0-only
"""Output locations for developer builds and owned, isolated setup stages."""
import json
import os
from pathlib import Path
import re


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
        base=self.root/'local/setup/builds'
        if self.stage==base or not self.stage.is_relative_to(base):
            raise ValueError('Stage root must be a directory below local/setup/builds/')
        relative=self.stage.relative_to(base)
        if len(relative.parts)!=1 or not re.fullmatch(r'[A-Za-z0-9_-]{1,80}',relative.name):
            raise ValueError('Build job ID must contain 1-80 ASCII letters, digits, underscores or hyphens')
        for path in (self.stage,*self.stage.parents):
            if path==self.root:
                break
            if path.is_symlink():
                raise ValueError('Build stage paths must not contain symlinks')

    def ensure(self):
        if self.stage is not None:
            self._check_stage()
            self.stage.mkdir(parents=True,exist_ok=True,mode=0o700)
            marker=self.stage/'.layout.json'
            info=self.root.stat()
            # Bind mounts use another path inside the build container, but the
            # checkout's filesystem identity remains the same.
            record=dict(version=1,project_identity=dict(device=info.st_dev,inode=info.st_ino),
                        kind='kirby-native-build-stage')
            if marker.exists() or marker.is_symlink():
                if marker.is_symlink() or not marker.is_file() or marker.stat().st_size>4096:
                    raise ValueError('Invalid build stage ownership record')
                if json.loads(marker.read_text())!=record:
                    raise ValueError('Build stage belongs to another layout or checkout')
            else:
                if any(self.stage.iterdir()):
                    raise ValueError('Refusing an unowned, nonempty build stage')
                fd=os.open(marker,os.O_CREAT|os.O_EXCL|os.O_WRONLY|os.O_NOFOLLOW,0o600)
                with os.fdopen(fd,'w') as stream:
                    stream.write(json.dumps(record)+'\n')
            for path in (self.generated,self.metadata,self.logs,self.workspace,self.build,self.translator_artifacts):
                if path.is_symlink():
                    raise ValueError('Build output directories must not be symlinks')
        for path in (self.generated,self.metadata,self.logs):
            path.mkdir(parents=True,exist_ok=True,mode=0o700)
