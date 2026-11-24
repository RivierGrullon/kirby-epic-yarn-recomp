# SPDX-License-Identifier: GPL-3.0-only
"""Independent PyQt window; all extraction/build work belongs to the worker."""
import os
import shutil
from pathlib import Path
import subprocess
import sys

from PyQt6.QtCore import Qt, QElapsedTimer, QLibraryInfo, QLocale, QSettings, QThread, QTimer, QTranslator
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import (QApplication, QBoxLayout, QCheckBox, QFrame, QHBoxLayout, QLabel,
                             QLineEdit, QMainWindow, QPlainTextEdit, QProgressBar, QScrollArea,
                             QVBoxLayout, QWidget)
from .setup_theme import Banner, Card, FabricButton, LanguagePicker, Linen, PhaseList, button_patch, label
from .setup_controller import Controller
from .setup_dialogs import choose_path, show_folder
from .setup_i18n import LANGUAGES, translate
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
    def __init__(self,bundle,parent=None,*,preferences=None):
        super().__init__(parent)
        self.bundle=bundle;self.controller=None;self.root=None;self.destination_path=None;self.launcher=None
        self.managed=False
        self.closing=False;self.elapsed=QElapsedTimer();self.preparation=None
        self.preferences = preferences if preferences is not None else QSettings('KirbyNative', 'Kirby Setup')
        saved = self.preferences.value('ui/language', 'en')
        self.language = saved if isinstance(saved, str) and saved in LANGUAGES else 'en'
        self.qt_translator = QTranslator(self)
        self.progress_session = None
        self.elapsed_seconds = None
        self.setWindowTitle('Kirby Setup')
        self.setMinimumSize(620, 520)
        self.resize(980, 650)
        icon = bundle.project/'packaging/linux/yarn-mascot-icon.png'
        if icon.is_file():
            self.setWindowIcon(QIcon(str(icon)))

        shell = Linen()
        self.setCentralWidget(shell)
        outer = QVBoxLayout(shell)
        outer.setContentsMargins(26, 12, 26, 15)
        outer.setSpacing(9)
        brand = QHBoxLayout()
        emblem = QLabel()
        emblem.setPixmap(button_patch('#ed9caf', 32))
        brand.addWidget(emblem)
        brand.addWidget(label('Kirby’s Epic Yarn', 'brand'), 1)
        language_label = label('Language')
        brand.addWidget(language_label)
        self.languages = LanguagePicker()
        self.languages.setCursor(Qt.CursorShape.PointingHandCursor)
        self.languages.setMinimumWidth(126)
        self.languages.setAccessibleName('Language')
        for code, name in LANGUAGES.items():
            self.languages.addItem(name, code)
        self.languages.setCurrentIndex(self.languages.findData(self.language))
        language_label.setBuddy(self.languages)
        brand.addWidget(self.languages)
        outer.addLayout(brand)

        scroll = QScrollArea()
        self.scroll = scroll
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.viewport().setObjectName('viewport')
        outer.addWidget(scroll, 1)
        body = QWidget()
        body.setObjectName('paper')
        layout = QVBoxLayout(body)
        layout.setContentsMargins(1, 3, 3, 4)
        layout.setSpacing(13)
        banner = Banner(bundle.project)
        layout.addWidget(banner)
        self.columns = QBoxLayout(QBoxLayout.Direction.LeftToRight)
        self.columns.setSpacing(14)
        layout.addLayout(self.columns)
        form = QVBoxLayout()
        form.setSpacing(11)
        self.columns.addLayout(form, 3)

        self.source = QLineEdit()
        self.source.setPlaceholderText('Disc image or extracted DATA folder')
        self.source.setAccessibleName('Game source')
        source_card = Card('&Your game copy', '#e6c586', self.source)
        source_card.body.addWidget(self.source)
        formats = 'Choose a Wii disc image (.wbfs, .iso, .rvz, .gcz) or a .7z archive.'
        self.source.setToolTip(formats)
        self.file_button = FabricButton('Choose disc…')
        self.file_button.setToolTip(formats)
        self.folder_button = FabricButton('DATA folder…')
        self.folder_button.setToolTip('Select the DATA folder if your game is already extracted.')
        self.file_button.clicked.connect(self.choose_file)
        self.folder_button.clicked.connect(self.choose_data)
        choices = QHBoxLayout()
        choices.addWidget(self.file_button)
        choices.addWidget(self.folder_button)
        choices.addStretch()
        source_card.body.addLayout(choices)
        form.addWidget(source_card)

        self.destination = QLineEdit(str(Path.home()/('Applications/Kirby Native' if sys.platform == 'darwin' else 'Games/Kirby Native')))
        self.destination.setAccessibleName('Installation folder')
        location = Card('&Installation folder', '#a8c8d8', self.destination)
        self.destination_button = FabricButton('Browse…')
        self.destination_button.clicked.connect(self.choose_destination)
        folder = QHBoxLayout()
        folder.addWidget(self.destination, 1)
        folder.addWidget(self.destination_button)
        location.body.addLayout(folder)
        self.desktop_shortcut = QCheckBox('Also create a desktop shortcut')
        location.body.addWidget(self.desktop_shortcut)
        form.addWidget(location)
        form.addStretch()

        progress_card = Card('Installation progress', '#b9cda6')
        self.stage = label('Ready to install', 'stage')
        self.stage.setAccessibleName('Installation status')
        progress_card.body.addWidget(self.stage)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        self.progress.setFixedHeight(10)
        self.progress.setAccessibleName('Current installation step progress')
        progress_card.body.addWidget(self.progress)
        self.progress_label = label('', 'secondary')
        self.progress_label.hide()
        progress_card.body.addWidget(self.progress_label)
        self.steps = PhaseList()
        progress_card.body.addWidget(self.steps)
        self.time_label = label('', 'secondary')
        self.time_label.hide()
        progress_card.body.addWidget(self.time_label)
        self.details_button = FabricButton('Show details')
        self.details_button.setCheckable(True)
        self.details_button.toggled.connect(self.toggle_details)
        self.logs_button = FabricButton('Open logs')
        self.logs_button.clicked.connect(self.open_logs)
        self.logs_button.setEnabled(False)
        self.logs_button.hide()
        tools = QHBoxLayout()
        tools.addWidget(self.details_button)
        tools.addWidget(self.logs_button)
        tools.addStretch()
        progress_card.body.addLayout(tools)
        progress_card.body.addStretch()
        self.columns.addWidget(progress_card, 2)
        self.details = QPlainTextEdit()
        self.details.setReadOnly(True)
        self.details.setAccessibleName('Installation details')
        self.details.setMaximumBlockCount(2000)
        self.details.setMinimumHeight(110)
        self.details.hide()
        layout.addWidget(self.details)
        layout.addStretch()
        scroll.setWidget(body)

        footer = QHBoxLayout()
        self.footer_note = label('Tools download once and can be reused.', 'secondary')
        self.footer_note.setToolTip('The first installation may take several minutes.')
        footer.addWidget(self.footer_note, 1)
        self.install_button = FabricButton('Install', primary=True)
        self.install_button.setMinimumWidth(118)
        self.install_button.setDefault(True)
        self.install_button.clicked.connect(self.install)
        self.cancel_button = FabricButton('Cancel')
        self.cancel_button.clicked.connect(self.cancel)
        self.open_folder_button = FabricButton('Open folder')
        self.open_folder_button.clicked.connect(lambda: show_folder(self, 'Installation folder', self.destination_path))
        self.open_button = FabricButton('Open launcher', primary=True)
        self.open_button.clicked.connect(self.open_launcher)
        for button in (self.install_button, self.cancel_button, self.open_folder_button, self.open_button):
            footer.addWidget(button)
        for button in (self.cancel_button, self.open_folder_button, self.open_button):
            button.setEnabled(False)
            button.hide()
        outer.addLayout(footer)
        # Retain source captions so changing language updates the existing widgets in place.
        self.captions = {w: w.text() for w in (
            language_label, banner.heading, banner.intro, source_card.heading, location.heading,
            progress_card.heading, self.file_button, self.folder_button, self.destination_button,
            self.desktop_shortcut, self.details_button, self.logs_button, self.footer_note,
            self.cancel_button, self.open_folder_button, self.open_button, self.install_button, self.stage)}
        self.tooltips = {w: w.toolTip() for w in (self.source, self.file_button, self.folder_button, self.footer_note)}
        self.accessible_names = {w: w.accessibleName() for w in (
            self.languages, self.source, self.destination, self.stage, self.progress, self.details)}
        self.languages.currentIndexChanged.connect(self.choose_language)
        self.fields=(self.source,self.destination,self.file_button,self.folder_button,self.destination_button,self.desktop_shortcut)
        self.source.textChanged.connect(self.form_changed);self.destination.textChanged.connect(self.form_changed)
        self.timer=QTimer(self);self.timer.setInterval(1000);self.timer.timeout.connect(self.update_time)
        self.form_changed()
        self.retranslate()

    def text(self, source):
        return translate(source, self.language)

    def caption(self, widget, source):
        self.captions[widget] = source
        value = self.text(source)
        if (widget is self.stage and widget.property('status') == 'failed'
                and self.language != 'en' and value == source):
            value = self.text('Installation stopped: {detail}').format(detail=source)
        widget.setText(value)

    def choose_language(self, index):
        code = self.languages.itemData(index)
        if code not in LANGUAGES or code == self.language:
            return
        self.language = code
        self.retranslate()
        self.preferences.setValue('ui/language', code)
        self.preferences.sync()
        if self.preferences.status() != QSettings.Status.NoError:
            self.statusBar().showMessage(self.text('Could not save the language preference.'), 5000)

    def retranslate(self):
        locale = QLocale(self.language)
        self.setLocale(locale)
        app = QApplication.instance()
        app.removeTranslator(self.qt_translator)
        directory = QLibraryInfo.path(QLibraryInfo.LibraryPath.TranslationsPath)
        if self.language != 'en' and self.qt_translator.load(locale, 'qtbase', '_', directory):
            app.installTranslator(self.qt_translator)
        self.setWindowTitle(self.text('Kirby Setup'))
        self.source.setPlaceholderText(self.text('Disc image or extracted DATA folder'))
        for widget, source in self.captions.items():
            self.caption(widget, source)
        for widget, source in self.tooltips.items():
            widget.setToolTip(self.text(source))
        for widget, source in self.accessible_names.items():
            widget.setAccessibleName(self.text(source))
        self.steps.set_language(self.text)
        self.show_progress_text()
        self.show_elapsed_time()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if hasattr(self, 'columns'):
            self.arrange_columns()

    def arrange_columns(self):
        direction = QBoxLayout.Direction.LeftToRight
        if self.width() < 820:
            direction = (QBoxLayout.Direction.BottomToTop if self.elapsed.isValid() or self.controller is not None
                         else QBoxLayout.Direction.TopToBottom)
        self.columns.setDirection(direction)

    def set_status(self, text, status):
        if self.stage.property('status') != status:
            self.stage.setProperty('status', status)
            self.stage.style().unpolish(self.stage)
            self.stage.style().polish(self.stage)
        self.caption(self.stage, text)

    def form_changed(self):
        busy=self.preparation is not None or (self.controller is not None and self.controller.busy)
        self.install_button.setEnabled(bool(self.source.text().strip() and self.destination.text().strip()) and not busy)
        if not busy and self.controller is None and not self.elapsed.isValid():
            self.caption(self.stage, 'Ready to install' if self.install_button.isEnabled() else
                         'Choose your game copy and installation folder.')

    def choose_file(self):
        filters = [self.text('Wii disc images')+' (*.wbfs *.iso *.rvz *.gcz *.7z)', self.text('All files')+' (*)']
        value = choose_path(self, 'Choose your disc image', filters=filters)
        if value:self.source.setText(value)

    def choose_data(self):
        value = choose_path(self, 'Choose your extracted DATA folder')
        if value:self.source.setText(value)

    def choose_destination(self):
        value = choose_path(self, 'Choose installation folder', self.destination.text())
        if value:self.destination.setText(value)

    def toggle_details(self,checked):
        self.details.setVisible(checked);self.caption(self.details_button, 'Hide details' if checked else 'Show details')
        if checked:
            QTimer.singleShot(0, self.reveal_details)

    def reveal_details(self):
        if self.details.isVisible():
            self.scroll.ensureWidgetVisible(self.details)

    def install(self):
        if self.preparation is not None or (self.controller is not None and self.controller.busy):return
        if not self.source.text().strip() or not self.destination.text().strip():return
        source=Path(self.source.text()).expanduser().absolute()
        self.destination_path=Path(self.destination.text()).expanduser().absolute()
        self.preparation=Preparation(self.bundle,self.destination_path,source,self)
        self.preparation.finished.connect(self.prepared)
        self.open_button.setDefault(False)
        self.install_button.set_primary(True)
        self.install_button.setDefault(True)
        for field in self.fields:field.setEnabled(False)
        self.install_button.setEnabled(False);self.open_button.setEnabled(False);self.open_folder_button.setEnabled(False)
        self.open_button.hide()
        self.open_folder_button.hide()
        self.logs_button.hide()
        self.logs_button.setEnabled(False)
        self.progress_label.hide()
        self.progress_session = None
        self.elapsed_seconds = None
        self.time_label.clear()
        self.time_label.hide()
        self.steps.set_states(['pending']*len(PHASES))
        self.set_status('Preparing your installation folder…', 'running')
        self.caption(self.install_button, 'Preparing…')
        self.progress.setRange(0,0)
        self.details.clear();self.launcher=None;self.elapsed.start();self.timer.start()
        self.caption(self.footer_note, '')
        self.arrange_columns()
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
            if sys.platform == 'darwin':
                python = (shutil.which('python3.12') or shutil.which('python3.13') or
                          shutil.which('python3.14') or shutil.which('python3.11') or shutil.which('python3'))
                if not python:
                    raise ValueError('Install Python 3.11 or later and add it to PATH.')
                command = [python, '-X', 'utf8', '-u', str(self.root/'tools/install.py')]
            else:
                if sys.platform == 'linux':
                    command.append('--supervise')
                if getattr(sys,'frozen',False) and not worker.is_file():
                    raise ValueError('The setup worker is missing. Extract the complete setup package again.')
            if self.controller is not None:self.controller.deleteLater()
            self.controller=Controller(self.root,command,self)
            self.controller.changed.connect(self.render);self.controller.finished.connect(self.finished)
            self.controller.output.connect(self.details.appendPlainText)
            self.controller.start(preparation.source)
        except (OSError,ValueError,subprocess.SubprocessError) as error:
            self.timer.stop();self.progress.setRange(0,100);self.progress.setValue(0)
            for field in self.fields:field.setEnabled(True)
            self.set_status(str(error), 'failed');self.details.appendPlainText(str(error));self.caption(self.install_button, 'Try again')
            self.form_changed()

    def render(self):
        if self.controller is None:return
        session=self.controller.session;busy=self.controller.busy
        for field in self.fields:field.setEnabled(not busy)
        self.cancel_button.setVisible(busy)
        self.cancel_button.setEnabled(busy and not self.controller.cancelling)
        self.install_button.setVisible(not busy)
        self.open_button.setVisible(not busy and self.launcher is not None)
        self.open_folder_button.setVisible(not busy and self.launcher is not None)
        self.open_button.setEnabled(not busy and self.launcher is not None)
        self.open_folder_button.setEnabled(not busy and self.launcher is not None)
        status = session.label if session.status == 'cancelled' else session.error or session.label
        self.set_status('Cancelling safely…' if busy and self.controller.cancelling else status, session.status)
        if busy and session.progress is None:self.progress.setRange(0,0)
        else:
            self.progress.setRange(0,100)
            self.progress.setValue(100 if session.status=='ready' else session.progress or 0)
        self.progress_session = session
        self.show_progress_text()
        states = list(session.phases)
        if session.status == 'cancelled' and session.phase is not None:
            states[session.phase] = 'cancelled'
        self.steps.set_states(states)
        self.logs_button.setVisible(session.log_path is not None)
        self.logs_button.setEnabled(session.log_path is not None)
        self.form_changed();self.update_time()

    def show_progress_text(self):
        session = self.progress_session
        if session is None:
            return
        terminal = {'ready': 'Finished', 'failed': 'Stopped', 'cancelled': 'Cancelled'}
        value = self.text(terminal.get(session.status, 'Working…'))
        measured = session.status not in terminal and session.progress is not None
        if measured:
            if session.unit == 'tasks':
                template = self.text('{percent}% · {done} / {total} build tasks')
                done, total = (self.locale().toString(n) for n in (session.completed, session.total))
            else:
                template = self.text('{percent}% · {done} / {total} MiB')
                done, total = (self.locale().toString(n/1048576, 'f', 1) for n in (session.completed, session.total))
            value = template.format(percent=session.progress, done=done, total=total)
        self.progress.setFormat(value)
        self.progress.setAccessibleDescription(value)
        self.progress_label.setText(value if measured else '')
        self.progress_label.setVisible(measured)

    def update_time(self):
        if self.elapsed.isValid():
            self.elapsed_seconds = self.elapsed.elapsed()//1000
            self.show_elapsed_time()

    def show_elapsed_time(self):
        if self.elapsed_seconds is not None:
            minutes, seconds = divmod(self.elapsed_seconds, 60)
            self.time_label.setText(self.text('Elapsed: {minutes} min {seconds:02d} sec').format(minutes=minutes, seconds=seconds))
            self.time_label.show()

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
                if self.managed:
                    from .external_launch import publish
                    publish(self.root,self.destination_path,session.result['data_root'])
                    self.caption(self.open_button, 'Play game')
                self.launcher=self.root/'build/native'/('kirby-launcher.exe' if os.name=='nt' else 'kirby-launcher')
                try:
                    shortcuts=[] if self.managed or sys.platform == 'darwin' else create_shortcuts(self.root,self.destination_path,desktop=self.desktop_shortcut.isChecked())
                    for path in shortcuts:self.details.appendPlainText('Created shortcut: '+str(path))
                except (OSError,ValueError) as error:
                    session.label='Your installation is ready. Could not create shortcuts: '+str(error)
                    self.details.appendPlainText(session.label)
            except (OSError,ValueError) as error:
                self.controller.session.status='failed';self.controller.session.error='Game built, but setup could not finish: '+str(error)
        self.caption(self.install_button, 'Install again' if self.launcher else 'Try again')
        self.install_button.set_primary(self.launcher is None)
        self.install_button.setDefault(self.launcher is None)
        self.open_button.setDefault(self.launcher is not None)
        self.render()
        if self.closing:self.close()

    def cancel(self):
        if self.controller is not None:self.controller.cancel()

    def open_logs(self):
        if self.controller and self.controller.session.log_path:
            show_folder(self, 'Open logs', self.controller.session.log_path.parent)

    def open_launcher(self):
        if self.launcher is None:return
        if self.managed:
            from .external_launch import play_command
            try:
                command,environment,working=play_command(self.destination_path)
                subprocess.Popen(command,cwd=working,env=environment,
                    **({'creationflags':0x208} if os.name=='nt' else {'start_new_session':True}))
            except (OSError,ValueError) as error:self.set_status('Could not start the game: '+str(error), 'failed')
            return
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
        except OSError as error:self.set_status('Could not open the launcher: '+str(error), 'failed')

    def closeEvent(self,event):
        if self.preparation is not None:
            self.closing=True;self.set_status('Finishing folder preparation before closing…', 'running');event.ignore()
        elif self.controller and self.controller.busy:
            self.closing=True;self.controller.cancel();event.ignore()
        else:
            QApplication.instance().removeTranslator(self.qt_translator)
            event.accept()
