#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Kirby Setup: a standalone PyQt frontend for the verified installation worker."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def configure_application(app):
    from PyQt6.QtGui import QColor,QFont,QPalette
    app.setStyle('Fusion')
    app.setFont(QFont('Segoe UI' if os.name=='nt' else 'DejaVu Sans',10))
    palette=QPalette()
    for role,color in ((QPalette.ColorRole.Window,'#10121b'),(QPalette.ColorRole.WindowText,'#f5eef3'),
        (QPalette.ColorRole.Base,'#191c29'),(QPalette.ColorRole.AlternateBase,'#222637'),
        (QPalette.ColorRole.Text,'#f5eef3'),(QPalette.ColorRole.Button,'#292b3d'),
        (QPalette.ColorRole.ButtonText,'#f5eef3'),(QPalette.ColorRole.Highlight,'#ae3d71'),
        (QPalette.ColorRole.HighlightedText,'#ffffff')):palette.setColor(role,QColor(color))
    palette.setColor(QPalette.ColorRole.PlaceholderText,QColor('#bcb6c9'))
    palette.setColor(QPalette.ColorGroup.Disabled,QPalette.ColorRole.Text,QColor('#9996a5'))
    palette.setColor(QPalette.ColorGroup.Disabled,QPalette.ColorRole.ButtonText,QColor('#9996a5'))
    app.setPalette(palette)
    app.setStyleSheet('''QLabel#title {font-size:25px;font-weight:600;color:#ffc3dc;}
        QLabel#secondary {color:#c1bdca;} QLineEdit {padding:7px;} QPushButton {padding:7px 12px;}
        QPushButton#primary:enabled {background:#ae3d71;color:white;} QPushButton#primary:hover:enabled {background:#c04880;}
        QProgressBar {min-height:22px;text-align:center;} QProgressBar::chunk {background:#ae3d71;}
        QListWidget {border:0;background:transparent;} QListWidget::item {padding:4px;}
        QPushButton:focus,QLineEdit:focus {border:2px solid #ffb4d2;}''')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--appdir',type=Path)
    parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--destination',type=Path,help='Reopen setup for an existing installation folder')
    args=parser.parse_args()
    from PyQt6.QtCore import QTimer
    from PyQt6.QtWidgets import QApplication
    from installer.setup_layout import Bundle
    from installer.setup_gui import SetupWindow
    if args.appdir or getattr(sys,'frozen',False):
        if args.appdir:base=args.appdir.resolve()
        else:
            candidates=(Path(sys.executable).resolve().parent,Path(sys.executable).resolve().parents[2])
            base=next((path for path in candidates if (path/'project').is_dir()),None)
            if base is None:parser.error('Keep Kirby Setup with its complete application folder.')
        project=base/'project'
        command=(str(Path(os.environ['APPIMAGE']).absolute()),) if os.environ.get('APPIMAGE') else (sys.executable,'--appdir',str(base))
        bundle=Bundle(project,base/'tools/runtime',base/'host-libs',(base/'revision').read_text().strip(),command,packaged=True)
    else:
        project=Path(__file__).resolve().parents[1]
        revision=subprocess.check_output(['git','-C',str(project),'rev-parse','HEAD'],text=True).strip()
        bundle=Bundle(project,project/'tools/runtime',project/'local/host-libs',revision,(sys.executable,str(project/'tools/setup_gui.py')))
    app=QApplication([sys.argv[0]]);app.setApplicationName('Kirby Setup');app.setOrganizationName('KirbyNative')
    configure_application(app)
    window=SetupWindow(bundle)
    if args.destination:window.destination.setText(str(args.destination))
    window.show()
    if args.self_test:QTimer.singleShot(150,window.close)
    return app.exec()


if __name__=='__main__':sys.exit(main())
