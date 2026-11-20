# SPDX-License-Identifier: GPL-3.0-only
import os
from pathlib import Path
import shutil
import tempfile
import tomllib
import uuid

from .installations import file_digest
from .jobs import run_process
from .platforms import WINDOWS, host_environment, is_link


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
        environment = host_environment(root/'local/host-libs')
        document = run_process(job, [candidate, '--build-info'], capture_limit=16384, timeout=10, env=environment)
        info = tomllib.loads(document.decode('utf-8'))
        if (info.get('schema_version') != 1 or info.get('program') != 'launcher'
                or not isinstance(info.get('build'), dict)
                or any(info['build'].get(key) != value for key, value in identity.items())):
            raise ValueError('Launcher build information does not match the game build')
        job.check_cancel()
        names = [library.name for library in libraries]+[name]
        qt = build/'qt-launcher'
        if (qt.is_dir() and not (destination/'qt-launcher/.bundled-revision').is_file()
                and qt.resolve() != (destination/'qt-launcher').resolve()):
            _publish_with_qt(qt, staging, destination, names, job)
        else:
            _publish_launcher(staging, destination, names, job)
    return destination/name


def _publish_with_qt(source, staging, destination, names, job):
    """Publish the Qt kit as a unit, including platform plugins and translations."""
    target = destination/'qt-launcher'
    new = staging/'qt-launcher'
    prior = staging/'previous-qt'
    if is_link(source) or is_link(target) or (target.exists() and not target.is_dir()):
        raise ValueError('Invalid Qt launcher directory')
    if not (source/('kirby-ui.exe' if WINDOWS else 'kirby-ui')).is_file():
        raise ValueError('Qt launcher executable is missing')
    new.mkdir()
    for parent, directories, files in os.walk(source):
        folder = Path(parent)
        output = new/folder.relative_to(source)
        for name in directories + files:
            if is_link(folder/name):
                raise ValueError('Qt launcher files must not be links')
        output.mkdir(exist_ok=True)
        for name in files:
            digest = file_digest(folder/name, job.check_cancel)
            shutil.copy2(folder/name, output/name)
            if file_digest(output/name, job.check_cancel) != digest:
                raise ValueError('Qt launcher changed while copying')
    job.check_cancel()
    if target.exists():
        target.rename(prior)
    try:
        new.rename(target)
        _publish_launcher(staging, destination, names, job)
    except BaseException:
        if target.exists():
            target.rename(new)
        if prior.exists():
            prior.rename(target)
        raise


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
