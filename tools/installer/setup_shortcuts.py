# SPDX-License-Identifier: GPL-3.0-only
"""Register the persistent launcher with the current user's desktop."""
import os
from pathlib import Path
import tempfile

from PyQt6.QtCore import QFile, QStandardPaths

from .platforms import WINDOWS, is_link
from .setup_layout import _atomic


def _directory(override, location):
    value=str(override) if override is not None else QStandardPaths.writableLocation(location)
    if not value or not Path(value).is_absolute():
        raise ValueError('The desktop did not provide a writable shortcut folder')
    return Path(value).resolve()


def _value(text):
    text=str(text)
    if '\0' in text:raise ValueError('A shortcut path contains a null character')
    return text.replace('\\','\\\\').replace('\n','\\n').replace('\r','\\r').replace('\t','\\t')


def _desktop_entry(root, starter):
    # Exec quoting has a second escaping layer inside desktop-entry string values.
    argument=str(starter).replace('%','%%')
    for character in ('\\','"','`','$'):argument=argument.replace(character,'\\'+character)
    icon=root/'packaging/linux/yarn-mascot-icon.png'
    return ('[Desktop Entry]\nType=Application\nName=Kirby\'s Epic Yarn\n'
            'Comment=Play Kirby\'s Epic Yarn\nExec='+_value('/bin/sh "'+argument+'"')+'\n'
            'Path='+_value(root)+'\nIcon='+_value(icon if icon.is_file() else 'applications-games')+'\n'
            'Terminal=false\nCategories=Game;\nStartupNotify=false\n').encode('utf-8')


def _windows_link(path, launcher):
    if is_link(path):raise ValueError('The shortcut must not be a filesystem link')
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kirby-shortcut-',dir=path.parent) as temporary:
        staged=Path(temporary)/'launcher.lnk'
        file=QFile(launcher.as_posix())
        if not file.link(staged.as_posix()):raise OSError('Could not create the Windows shortcut: '+file.errorString())
        os.replace(staged,path)


def create_shortcuts(root, destination, *, desktop=False, applications=None, desktop_directory=None):
    """Update the menu entry and optionally create an executable desktop shortcut."""
    root,destination=Path(root).resolve(),Path(destination).resolve()
    launcher=root/'build/native'/('kirby-launcher.exe' if WINDOWS else 'kirby-launcher')
    launcher.relative_to(destination)
    if not launcher.is_file():raise ValueError('The installed launcher is missing')
    starter=destination/'Play Kirby.sh'
    if not WINDOWS and not starter.is_file():raise ValueError('The installed Play Kirby.sh shortcut is missing')
    menu=_directory(applications,QStandardPaths.StandardLocation.ApplicationsLocation)
    folders=[menu]
    if desktop:folders.append(_directory(desktop_directory,QStandardPaths.StandardLocation.DesktopLocation))
    name="Kirby's Epic Yarn.lnk" if WINDOWS else 'kirby-epic-yarn.desktop'
    paths=[]
    for folder in folders:
        path=folder/name
        if WINDOWS:_windows_link(path,launcher)
        else:_atomic(path,_desktop_entry(root,starter),executable=folder!=menu)
        paths.append(path)
    return paths
