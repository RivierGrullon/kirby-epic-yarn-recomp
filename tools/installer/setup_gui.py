# SPDX-License-Identifier: GPL-3.0-only
"""Independent PyQt window; all extraction/build work belongs to the worker."""
import os
from pathlib import Path
import subprocess
import sys

from PyQt6.QtCore import Qt, QElapsedTimer, QSize, QThread, QTimer, QUrl
from PyQt6.QtGui import QColor, QDesktopServices, QIcon
from PyQt6.QtWidgets import (QApplication,QCheckBox,QFileDialog,QFrame,QHBoxLayout,QLabel,QLineEdit,
    QListWidget,QListWidgetItem,QMainWindow,QPlainTextEdit,QProgressBar,QPushButton,QScrollArea,
    QStyle,QVBoxLayout,QWidget)
from .setup_controller import Controller
from .setup_layout import prepare_destination,finish_destination
from .setup_model import PHASES
from .setup_shortcuts import create_shortcuts


class Preparation(QThread):
    def __init__(self,bundle,destination,source,parent):
        super().__init__(parent);self.bundle=bundle;self.destination=destination;self.source=source
        self.root=None;self.error=''

    def run(self):
        try:
            if not self.source.exists():raise ValueError('Choose an existing disc image or DATA folder.')
            self.root=prepare_destination(self.bundle,self.destination)
        except (OSError,ValueError,subprocess.SubprocessError) as error:self.error=str(error)


class SetupWindow(QMainWindow):
    def __init__(self,bundle,parent=None):
        super().__init__(parent)
        self.bundle=bundle;self.controller=None;self.root=None;self.destination_path=None;self.launcher=None
        self.closing=False;self.elapsed=QElapsedTimer();self.preparation=None
        self.setWindowTitle('Kirby Setup');self.resize(760,690);self.setMinimumSize(620,560)
        icon=bundle.project/'packaging/linux/yarn-mascot-icon.png'
        if icon.is_file():self.setWindowIcon(QIcon(str(icon)))
        shell=QWidget();self.setCentralWidget(shell);outer=QVBoxLayout(shell);outer.setContentsMargins(0,0,0,0)
        scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setFrameShape(QFrame.Shape.NoFrame)
        body=QWidget();scroll.setWidget(body);outer.addWidget(scroll,1);layout=QVBoxLayout(body)
        layout.setContentsMargins(28,24,28,24);layout.setSpacing(12)
        title=QLabel('Kirby Setup');title.setObjectName('title');layout.addWidget(title)
        intro=QLabel('Install Kirby’s Epic Yarn from your own USA copy (RK5E01 Rev 0).')
        intro.setWordWrap(True);layout.addWidget(intro)
        self.source=QLineEdit();self.source.setPlaceholderText('Select your disc image or extracted DATA folder')
        self.source.setAccessibleName('Game source')
        self.file_button=QPushButton('Choose disc…');self.folder_button=QPushButton('DATA folder…')
        self.file_button.clicked.connect(self.choose_file);self.folder_button.clicked.connect(self.choose_data)
        label=QLabel('&Your game copy');label.setBuddy(self.source);layout.addWidget(label)
        row=QHBoxLayout();row.addWidget(self.source,1);row.addWidget(self.file_button);row.addWidget(self.folder_button);layout.addLayout(row)
        self.destination=QLineEdit(str(Path.home()/'Games/Kirby Native'));self.destination.setAccessibleName('Installation folder')
        label=QLabel('&Installation folder');label.setBuddy(self.destination);layout.addWidget(label)
        self.destination_button=QPushButton('Browse…');self.destination_button.clicked.connect(self.choose_destination)
        row=QHBoxLayout();row.addWidget(self.destination,1);row.addWidget(self.destination_button);layout.addLayout(row)
        self.desktop_shortcut=QCheckBox('Also create a desktop shortcut')
        layout.addWidget(self.desktop_shortcut)
        note=QLabel('Tools download once and are reused. Installation may take several minutes.')
        note.setWordWrap(True);note.setObjectName('secondary');layout.addWidget(note)
        line=QFrame();line.setFrameShape(QFrame.Shape.HLine);layout.addWidget(line)
        self.stage=QLabel('Ready to install');self.stage.setWordWrap(True);self.stage.setTextFormat(Qt.TextFormat.PlainText)
        layout.addWidget(self.stage)
        self.progress=QProgressBar();self.progress.setRange(0,100);self.progress.setValue(0)
        self.progress.setAccessibleName('Current installation step progress');layout.addWidget(self.progress)
        self.time_label=QLabel('');self.time_label.setObjectName('secondary');layout.addWidget(self.time_label)
        self.steps=QListWidget();self.steps.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.steps.setSelectionMode(QListWidget.SelectionMode.NoSelection);self.steps.setFixedHeight(130)
        self.steps.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        for name in PHASES:
            item=QListWidgetItem(name+' · Waiting');item.setSizeHint(QSize(0,24));self.steps.addItem(item)
        self.steps.setAccessibleName('Installation steps');layout.addWidget(self.steps)
        self.details_button=QPushButton('Show details');self.details_button.setCheckable(True)
        self.details_button.toggled.connect(self.toggle_details)
        self.logs_button=QPushButton('Open logs');self.logs_button.setEnabled(False);self.logs_button.clicked.connect(self.open_logs)
        row=QHBoxLayout();row.addWidget(self.details_button);row.addWidget(self.logs_button);row.addStretch();layout.addLayout(row)
        self.details=QPlainTextEdit();self.details.setReadOnly(True);self.details.setMaximumBlockCount(2000)
        self.details.setMinimumHeight(100);self.details.hide();layout.addWidget(self.details)
        layout.addStretch()
        self.install_button=QPushButton('Install');self.install_button.setObjectName('primary');self.install_button.setDefault(True)
        self.install_button.clicked.connect(self.install)
        self.cancel_button=QPushButton('Cancel');self.cancel_button.setEnabled(False);self.cancel_button.clicked.connect(self.cancel)
        self.open_button=QPushButton('Open launcher');self.open_button.setEnabled(False);self.open_button.clicked.connect(self.open_launcher)
        self.open_folder_button=QPushButton('Open folder');self.open_folder_button.setEnabled(False)
        self.open_folder_button.clicked.connect(lambda:QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.destination_path))))
        footer=QWidget();row=QHBoxLayout(footer);row.setContentsMargins(28,8,28,20)
        row.addWidget(self.install_button);row.addWidget(self.cancel_button);row.addStretch()
        row.addWidget(self.open_folder_button);row.addWidget(self.open_button);outer.addWidget(footer)
        self.fields=(self.source,self.destination,self.file_button,self.folder_button,self.destination_button,self.desktop_shortcut)
        self.source.textChanged.connect(self.form_changed);self.destination.textChanged.connect(self.form_changed)
        self.timer=QTimer(self);self.timer.setInterval(1000);self.timer.timeout.connect(self.update_time)
        self.form_changed()

    def form_changed(self):
        busy=self.preparation is not None or (self.controller is not None and self.controller.busy)
        self.install_button.setEnabled(bool(self.source.text().strip() and self.destination.text().strip()) and not busy)

    def choose_file(self):
        value,_=QFileDialog.getOpenFileName(self,'Choose your disc image','',
            'Wii disc images (*.wbfs *.iso *.rvz *.7z);;All files (*)')
        if value:self.source.setText(value)

    def choose_data(self):
        value=QFileDialog.getExistingDirectory(self,'Choose your extracted DATA folder')
        if value:self.source.setText(value)

    def choose_destination(self):
        value=QFileDialog.getExistingDirectory(self,'Choose installation folder',self.destination.text())
        if value:self.destination.setText(value)

    def toggle_details(self,checked):
        self.details.setVisible(checked);self.details_button.setText('Hide details' if checked else 'Show details')

    def install(self):
        if self.preparation is not None or (self.controller is not None and self.controller.busy):return
        if not self.source.text().strip() or not self.destination.text().strip():return
        source=Path(self.source.text()).expanduser().absolute()
        self.destination_path=Path(self.destination.text()).expanduser().absolute()
        self.preparation=Preparation(self.bundle,self.destination_path,source,self)
        self.preparation.finished.connect(self.prepared)
        for field in self.fields:field.setEnabled(False)
        self.install_button.setEnabled(False);self.open_button.setEnabled(False);self.open_folder_button.setEnabled(False)
        self.stage.setText('Preparing your installation folder…');self.progress.setRange(0,0)
        self.details.clear();self.launcher=None;self.elapsed.start();self.timer.start()
        self.preparation.start()

    def prepared(self):
        preparation=self.preparation;self.preparation=None
        preparation.deleteLater()
        if self.closing:self.timer.stop();self.close();return
        try:
            if preparation.error:raise ValueError(preparation.error)
            self.root=preparation.root
            worker=self.bundle.runtime/('kirby-setup-worker.exe' if os.name=='nt' else 'kirby-setup-worker')
            command=[str(worker)] if worker.is_file() else [sys.executable,'-X','utf8','-u',str(self.root/'tools/install.py')]
            if sys.platform=='linux':command.append('--supervise')
            if getattr(sys,'frozen',False) and not worker.is_file():raise ValueError('The setup worker is missing. Extract the complete setup package again.')
            if self.controller is not None:self.controller.deleteLater()
            self.controller=Controller(self.root,command,self)
            self.controller.changed.connect(self.render);self.controller.finished.connect(self.finished)
            self.controller.output.connect(self.details.appendPlainText)
            self.controller.start(preparation.source)
        except (OSError,ValueError,subprocess.SubprocessError) as error:
            self.timer.stop();self.progress.setRange(0,100);self.progress.setValue(0);self.progress.setFormat('Stopped')
            for field in self.fields:field.setEnabled(True)
            self.stage.setText(str(error));self.details.appendPlainText(str(error));self.install_button.setText('Try again')
            self.form_changed()

    def render(self):
        if self.controller is None:return
        session=self.controller.session;busy=self.controller.busy
        for field in self.fields:field.setEnabled(not busy)
        self.cancel_button.setEnabled(busy and not self.controller.cancelling)
        self.open_button.setEnabled(not busy and self.launcher is not None)
        self.open_folder_button.setEnabled(not busy and self.launcher is not None)
        self.stage.setText('Cancelling safely…' if busy and self.controller.cancelling else session.error or session.label)
        if busy and session.progress is None:self.progress.setRange(0,0)
        else:
            self.progress.setRange(0,100)
            self.progress.setValue(100 if session.status=='ready' else session.progress or 0)
            self.progress.setFormat('Finished' if session.status=='ready' else 'Stopped' if session.status=='failed'
                                    else 'Cancelled' if session.status=='cancelled' else session.progress_text)
        labels={'pending':'Waiting','running':'In progress','done':'Done','failed':'Failed'}
        colors={'pending':'#bcb6c9','running':'#ffc3dc','done':'#99dfb3','failed':'#ffb2b7'}
        for i,status in enumerate(session.phases):
            self.steps.item(i).setText(PHASES[i]+' · '+labels[status])
            self.steps.item(i).setForeground(QColor(colors[status]))
        self.logs_button.setEnabled(session.log_path is not None)
        self.form_changed();self.update_time()

    def update_time(self):
        if self.elapsed.isValid():
            seconds=self.elapsed.elapsed()//1000;self.time_label.setText(f'Elapsed: {seconds//60} min {seconds%60:02d} sec')

    def finished(self,status):
        self.timer.stop()
        if status=='ready':
            try:
                from .installations import InstallationStore
                session=self.controller.session;store=InstallationStore(self.root)
                active=store.read_active()
                if not active or active['current']!=session.result['installation_id']:
                    raise ValueError('The selected installation changed. Run setup again.')
                store.manifest(active['current'])
                finish_destination(self.root,self.destination_path,session.result['data_root'])
                self.launcher=self.root/'build/native'/('kirby-launcher.exe' if os.name=='nt' else 'kirby-launcher')
                try:
                    shortcuts=create_shortcuts(self.root,self.destination_path,desktop=self.desktop_shortcut.isChecked())
                    for path in shortcuts:self.details.appendPlainText('Created shortcut: '+str(path))
                except (OSError,ValueError) as error:
                    session.label='Your installation is ready. Could not create shortcuts: '+str(error)
                    self.details.appendPlainText(session.label)
            except (OSError,ValueError) as error:
                self.controller.session.status='failed';self.controller.session.error='Game built, but setup could not finish: '+str(error)
        self.install_button.setText('Install again' if self.launcher else 'Try again');self.render()
        if self.closing:self.close()

    def cancel(self):
        if self.controller is not None:self.controller.cancel()

    def open_logs(self):
        if self.controller and self.controller.session.log_path:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.controller.session.log_path.parent)))

    def open_launcher(self):
        if self.launcher is None:return
        environment=dict(os.environ)
        for key in list(environment):
            if key.startswith(('QT_','QML','PYTHON')):environment.pop(key,None)
        original=environment.pop('LD_LIBRARY_PATH_ORIG','')
        if os.name!='nt':environment['LD_LIBRARY_PATH']=str(self.root/'local/host-libs')+(os.pathsep+original if original else '')
        else:
            import ctypes
            ctypes.windll.kernel32.SetDllDirectoryW(None)
        try:
            subprocess.Popen([str(self.launcher)],cwd=self.root,env=environment,
                **({'creationflags':0x208} if os.name=='nt' else {'start_new_session':True}))
        except OSError as error:self.stage.setText('Could not open the launcher: '+str(error))

    def closeEvent(self,event):
        if self.preparation is not None:
            self.closing=True;self.stage.setText('Finishing folder preparation before closing…');event.ignore()
        elif self.controller and self.controller.busy:
            self.closing=True;self.controller.cancel();event.ignore()
        else:event.accept()
