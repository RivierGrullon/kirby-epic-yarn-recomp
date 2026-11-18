# SPDX-License-Identifier: GPL-3.0-only
import os
from pathlib import Path
import shutil
import tempfile
import tomllib
import uuid

from .installations import file_digest
from .jobs import run_process
from .platforms import WINDOWS, is_link


def install_launcher(job, root, build, identity):
    name = 'kirby-launcher.exe' if WINDOWS else 'kirby-launcher'
    source = build/name
    expected = file_digest(source, job.check_cancel)
    destination = root/'build/native'
    for directory in (root/'build', destination):
        if is_link(directory):
            raise ValueError('Launcher output directory must not be a link')
        directory.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.launcher-', dir=destination) as temporary:
        staging = Path(temporary)
        candidate = staging/name
        shutil.copyfile(source, candidate, follow_symlinks=False)
        candidate.chmod(0o700)
        if file_digest(candidate, job.check_cancel) != expected:
            raise ValueError('Launcher changed while copying')
        libraries = sorted(path for path in build.iterdir() if path.suffix.lower()=='.dll') if WINDOWS else []
        for library in libraries:
            digest = file_digest(library, job.check_cancel)
            shutil.copyfile(library, staging/library.name, follow_symlinks=False)
            if file_digest(staging/library.name) != digest:
                raise ValueError('Launcher library changed while copying')
        environment = dict(os.environ)
        if not WINDOWS and (root/'local/host-libs').is_dir():
            environment['LD_LIBRARY_PATH'] = str(root/'local/host-libs')+os.pathsep+environment.get('LD_LIBRARY_PATH', '')
        document = run_process(job, [candidate, '--build-info'], capture_limit=16384, timeout=10, env=environment)
        info = tomllib.loads(document.decode('utf-8'))
        if (info.get('schema_version') != 1 or info.get('program') != 'launcher'
                or not isinstance(info.get('build'), dict)
                or any(info['build'].get(key) != value for key, value in identity.items())):
            raise ValueError('Launcher build information does not match the game build')
        job.check_cancel()
        _publish_launcher(staging, destination, [library.name for library in libraries]+[name], job)
    return destination/name


def _publish_launcher(staging, destination, names, job):
    """Replace the verified launcher and DLLs, restoring earlier files on failure."""
    backup=staging/'previous';backup.mkdir()
    prior=set();changed=[];pending=[]
    # Back up all existing products before the first replacement. Copying also
    # leaves a running Windows launcher intact if its files cannot be replaced.
    for name in names:
        target=destination/name
        if is_link(target):
            raise ValueError(f'Launcher output must not be a link: {target}')
        if target.exists():
            expected=file_digest(target,job.check_cancel)
            if expected == file_digest(staging/name,job.check_cancel):
                continue
            shutil.copyfile(target,backup/name,follow_symlinks=False)
            (backup/name).chmod(0o700 if target.suffix.lower()!='.dll' else 0o600)
            if file_digest(backup/name,job.check_cancel)!=expected:
                raise ValueError('Previous launcher changed while backing up')
            prior.add(name)
        pending.append(name)
    job.check_cancel()
    try:
        for name in pending:
            os.replace(staging/name,destination/name)
            changed.append(name)
    except BaseException as error:
        failures=[]
        for name in reversed(changed):
            try:
                if name in prior:
                    os.replace(backup/name,destination/name)
                else:
                    (destination/name).unlink()
            except OSError as restore_error:
                failures.append(f'{name}: {restore_error}')
        if failures:
            # Keep the last known good bytes outside the temporary directory.
            recovery=destination/('.launcher-recovery-'+uuid.uuid4().hex)
            os.rename(backup,recovery)
            raise ValueError(f'Launcher deployment failed and could not restore all files. '
                             f'Previous files are in {recovery}: '+ '; '.join(failures)) from error
        raise
