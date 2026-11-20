# SPDX-License-Identifier: GPL-3.0-only
"""Deploy the disc-independent frontend from a release into a persistent install."""
import os
from pathlib import Path
import shutil
import tempfile

from .platforms import WINDOWS, is_link


def validate(directory, windows=WINDOWS):
    directory = Path(directory)
    if windows:
        files = ('kirby-ui.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
                 'platforms/qwindows.dll', 'platforms/qoffscreen.dll')
    else:
        files = ('kirby-ui', 'qt.conf', 'lib/libQt6Core.so.6', 'lib/libQt6Gui.so.6',
                 'lib/libQt6Widgets.so.6', 'plugins/platforms/libqxcb.so', 'plugins/platforms/libqoffscreen.so')
    for name in files:
        if not (directory/name).is_file():
            raise ValueError(f'Incomplete Qt launcher deployment: missing {name}')
    return directory/files[0]


def copy_payload(source, destination, windows=WINDOWS):
    source = Path(source).resolve(strict=True)
    validate(source, windows)
    # Qt's Linux deployment contains relative SONAME links. Only copy links to
    # regular files within the kit, so moving or unmounting Setup is safe.
    for parent, dirs, files in os.walk(source):
        for name in dirs + files:
            entry = Path(parent)/name
            if is_link(entry) and (not entry.is_symlink() or entry.readlink().is_absolute() or
                                   not entry.resolve().is_relative_to(source) or not entry.is_file()):
                raise ValueError(f'Invalid link in Qt launcher deployment: {entry}')
    shutil.copytree(source, destination, symlinks=True)
    validate(destination, windows)


def install_bundled(bundle, root):
    """Each source revision owns its frontend. Repairs leave user data alone."""
    parent = root/'build/native'
    target = parent/'qt-launcher'
    for path in (parent.parent, parent, target):
        if is_link(path):
            raise ValueError('Invalid installed Qt launcher directory')
    parent.mkdir(parents=True, exist_ok=True)
    marker = target/'.bundled-revision'
    if not is_link(marker) and marker.is_file() and marker.read_text().strip() == bundle.revision:
        try:
            validate(target)
            return
        except ValueError:
            pass  # Restore a missing executable, library or platform plugin on retry.
    with tempfile.TemporaryDirectory(prefix='.qt-', dir=parent) as tmp:
        new = Path(tmp)/'new'
        old = Path(tmp)/'old'
        copy_payload(bundle.runtime.parent/'qt-launcher', new)
        (new/'.bundled-revision').write_text(bundle.revision+'\n', encoding='ascii')
        if target.exists():
            target.rename(old)
        try:
            new.rename(target)
        except OSError:
            if old.exists():
                old.rename(target)
            raise
