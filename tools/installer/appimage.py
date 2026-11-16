# SPDX-License-Identifier: GPL-3.0-only
"""Keep generated code and user data outside the read-only AppImage mount."""
import os
from pathlib import Path
import re
import shutil
import tempfile
import uuid


def prepare_appimage(appdir, data_home):
    import fcntl
    appdir=Path(appdir).resolve()
    revision=(appdir/'revision').read_text().strip()
    if not re.fullmatch('[a-f0-9]{40}',revision):
        raise ValueError('Invalid AppImage source revision')
    versions=Path(data_home).absolute()/'kirby-native/versions'
    versions.mkdir(parents=True,exist_ok=True,mode=0o700)
    root=versions/revision
    with (versions/'.prepare.lock').open('a+b') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        if root.is_symlink():
            raise ValueError('AppImage source directory must not be a symlink')
        if not root.exists():
            with tempfile.TemporaryDirectory(prefix='.prepare-',dir=versions) as temporary:
                staged=Path(temporary)/'project'
                shutil.copytree(appdir/'project',staged,symlinks=True)
                os.rename(staged,root)
        if not (root/'tools/kirby.py').is_file():
            raise ValueError('Incomplete AppImage source copy')
        for path,target in ((root/'tools/runtime',appdir/'tools/runtime'),
                            (root/'local/host-libs',appdir/'host-libs')):
            path.parent.mkdir(parents=True,exist_ok=True)
            if path.exists() and not path.is_symlink():
                raise ValueError('Unexpected existing application tool directory: '+str(path))
            link=path.with_name('.link-'+uuid.uuid4().hex)
            try:
                link.symlink_to(target,target_is_directory=True)
                os.replace(link,path)
            finally:
                link.unlink(missing_ok=True)
    return root


def launch_appimage(appdir, arguments):
    appdir=Path(appdir).resolve()
    data=os.environ.get('XDG_DATA_HOME') or str(Path.home()/'.local/share')
    root=prepare_appimage(appdir,data)
    environment=dict(os.environ,KIRBY_PROJECT_ROOT=str(root))
    environment['LD_LIBRARY_PATH']=str(appdir/'host-libs')+(
        os.pathsep+environment['LD_LIBRARY_PATH'] if environment.get('LD_LIBRARY_PATH') else '')
    launcher=root/'build/native/kirby-launcher'
    os.chdir(root)
    os.execve(launcher,[str(launcher),*arguments],environment)
