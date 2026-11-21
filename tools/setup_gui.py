#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Kirby Setup: a standalone PyQt frontend for the verified installation worker."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def configure_application(app, project=None):
    from installer.setup_theme import configure_application as apply_theme
    apply_theme(app, project or Path(__file__).resolve().parents[1])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--appdir',type=Path)
    parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--destination',type=Path,help='Reopen setup for an existing installation folder')
    parser.add_argument('--managed',action='store_true',help='Keep third-party launcher saves in the destination folder')
    args=parser.parse_args()
    from PyQt6.QtCore import Qt, QTimer
    from PyQt6.QtWidgets import QApplication
    from installer.setup_layout import Bundle
    from installer.setup_gui import SetupWindow
    if args.appdir or getattr(sys,'frozen',False):
        if args.appdir:base=args.appdir.resolve()
        else:
            parents=Path(sys.executable).resolve().parents
            candidates=(*parents[:1],*parents[2:3])
            base=next((path for path in candidates if (path/'project').is_dir()),None)
            if base is None:parser.error('Keep Kirby Setup with its complete application folder.')
        project=base/'project'
        command=(str(Path(os.environ['APPIMAGE']).absolute()),) if os.environ.get('APPIMAGE') else (sys.executable,'--appdir',str(base))
        bundle=Bundle(project,base/'tools/runtime',base/'host-libs',(base/'revision').read_text().strip(),command,packaged=True)
    else:
        project=Path(__file__).resolve().parents[1]
        revision=subprocess.check_output(['git','-C',str(project),'rev-parse','HEAD'],text=True).strip()
        bundle=Bundle(project,project/'tools/runtime',project/'local/host-libs',revision,(sys.executable,str(project/'tools/setup_gui.py')))
    QApplication.setAttribute(Qt.ApplicationAttribute.AA_DontUseNativeDialogs)
    app=QApplication([sys.argv[0]]);app.setApplicationName('Kirby Setup');app.setOrganizationName('KirbyNative')
    configure_application(app, project)
    window=SetupWindow(bundle)
    window.managed=args.managed
    if args.destination:window.destination.setText(str(args.destination))
    if args.managed:
        window.destination.setReadOnly(True)
        window.destination_button.hide()
        window.desktop_shortcut.hide()
    window.show()
    if args.self_test:QTimer.singleShot(150,window.close)
    return app.exec()


if __name__=='__main__':sys.exit(main())
