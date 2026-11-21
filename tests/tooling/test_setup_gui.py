# SPDX-License-Identifier: GPL-3.0-only
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
try:
    from PyQt6.QtWidgets import QApplication
except ImportError:
    QApplication=None

WORKER = '''import json,pathlib,sys,time
r=json.loads(input());seq=0
def emit(stage,state,**fields):
 global seq
 seq+=1;print(json.dumps(dict(version=1,job_id=r['id'],sequence=seq,stage=stage,state=state,**fields)),flush=True)
emit('prepare','started',completed=0,total=100)
emit('compile','progress',completed=5,total=10,unit='tasks')
name=pathlib.Path(r['source']).stem
if name=='cancel':
 c=json.loads(input())
 if c['operation']!='cancel' or c['id']!=r['id']:sys.exit(3)
 emit('install','cancelled',error='Setup cancelled safely');sys.exit(2)
if name=='bad':
 print('not json',flush=True);time.sleep(3);sys.exit(1)
if name=='fail':
 emit('install','failed',error='Synthetic compilation failed');sys.exit(1)
root=pathlib.Path.cwd();identifier='a'*64
exe='WiiCompiled.exe' if sys.platform=='win32' else 'WiiCompiled'
emit('install','complete',result=dict(installation_id=identifier,committed=True,source=r['source'],
 data_root=str(root/'DATA'),executable=str(root/'local/setup/installations'/identifier/exe)))
if name=='crash':sys.exit(1)
'''


@unittest.skipIf(QApplication is None,'Install tools/requirements-setup.txt for Qt tests')
class SetupEntryTests(unittest.TestCase):
    def test_frozen_entry_handles_shallow_and_nested_application_paths(self):
        import setup_gui
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            (base/'project').mkdir()
            (base/'revision').write_text('a'*40)
            for parents in ((base,), (base,base.parent),
                            (base/'tools/setup',base/'tools',base)):
                with (self.subTest(parents=parents),
                      patch.object(sys,'argv',['Kirby-Setup.exe']),
                      patch.object(sys,'frozen',True,create=True),
                      patch.object(setup_gui,'Path') as path,
                      patch.object(setup_gui,'configure_application'),
                      patch('PyQt6.QtWidgets.QApplication') as app,
                      patch('installer.setup_gui.SetupWindow') as window):
                    path.return_value.resolve.return_value=Mock(parents=parents)
                    app.return_value.exec.return_value=0
                    self.assertEqual(setup_gui.main(),0)
                    self.assertEqual(window.call_args.args[0].project,base/'project')
                    window.return_value.show.assert_called_once()


@unittest.skipIf(QApplication is None,'Install tools/requirements-setup.txt for Qt tests')
class SetupGuiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or QApplication([])

    def setUp(self):
        from installer.setup_controller import Controller
        self.temp=tempfile.TemporaryDirectory(prefix='Qt worker literal $ ');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.script=self.root/'worker.py';self.script.write_text(WORKER)
        from PyQt6.QtCore import QSettings
        self.preferences=QSettings(str(self.root/'ui.ini'),QSettings.Format.IniFormat)
        self.controller=Controller(self.root,[sys.executable,'-u',str(self.script)],grace_ms=100)
        self.addCleanup(self.stop)

    def stop(self):
        if self.controller.busy:
            self.controller.cancel()
            self.wait(lambda:not self.controller.busy)
        self.controller.deleteLater();self.app.processEvents()

    def wait(self,predicate):
        deadline=time.monotonic()+6
        while not predicate() and time.monotonic()<deadline:
            self.app.processEvents();time.sleep(.005)
        self.assertTrue(predicate(),'Qt process did not settle')

    def test_actual_worker_success_and_retry_use_literal_source_and_process_exit(self):
        source=self.root/"own ' $(literal).wbfs"
        self.controller.start(source)
        self.wait(lambda:not self.controller.busy)
        self.assertEqual(self.controller.session.status,'ready')
        self.assertEqual(self.controller.session.result['source'],str(source))
        self.controller.start(self.root/'fail.wbfs');self.wait(lambda:not self.controller.busy)
        self.assertEqual(self.controller.session.status,'failed')
        self.assertIn('Synthetic compilation',self.controller.session.error)

    def test_cancel_reaches_worker_and_ui_event_loop_remains_responsive(self):
        from PyQt6.QtCore import QTimer
        ticks=[];timer=QTimer();timer.timeout.connect(lambda:ticks.append(1));timer.start(5)
        self.controller.start(self.root/'cancel.wbfs')
        self.wait(lambda:self.controller.session.progress==50)
        self.assertTrue(ticks)
        self.controller.cancel();self.wait(lambda:not self.controller.busy);timer.stop()
        self.assertEqual(self.controller.session.status,'cancelled')

    def test_crash_after_complete_and_invalid_json_never_enable_launcher(self):
        for name in ('crash','bad'):
            self.controller.start(self.root/(name+'.wbfs'));self.wait(lambda:not self.controller.busy)
            self.assertEqual(self.controller.session.status,'failed')
            self.assertIsNone(self.controller.session.result)

    def test_missing_worker_has_actionable_failure_and_allows_retry(self):
        self.controller.worker_command=[str(self.root/'missing-worker')]
        self.controller.start(self.root/'own.wbfs');self.wait(lambda:not self.controller.busy)
        self.assertEqual(self.controller.session.status,'failed')
        self.assertTrue(self.controller.session.error)

    def test_window_form_does_not_install_without_a_source(self):
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        bundle=Bundle(self.root,self.root/'runtime',self.root/'libs','a'*40)
        window=SetupWindow(bundle,preferences=self.preferences);window.show();self.app.processEvents()
        self.assertFalse(window.install_button.isEnabled())
        self.assertFalse(window.open_button.isEnabled())
        self.assertEqual(window.progress.maximum(),100)
        window.source.setText(str(self.root/'own.wbfs'));self.app.processEvents()
        self.assertTrue(window.install_button.isEnabled())
        window.close();window.deleteLater();self.app.processEvents()

    def test_setup_buttons_open_widget_pickers_and_preserve_selection_on_cancel(self):
        from PyQt6.QtCore import QTimer
        from PyQt6.QtWidgets import QFileDialog
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        window = SetupWindow(Bundle(self.root, self.root/'runtime', self.root/'libs', 'a'*40),
                             preferences=self.preferences)
        self.addCleanup(window.close)
        window.show()
        image = self.root/'own copy.wbfs'
        image.touch()
        for button, field, mode, target in (
                (window.file_button, window.source, QFileDialog.FileMode.ExistingFile, image),
                (window.folder_button, window.source, QFileDialog.FileMode.Directory, self.root),
                (window.destination_button, window.destination, QFileDialog.FileMode.Directory, self.root)):
            for accept in (False, True):
                original = field.text()
                observed = []

                def inspect():
                    dialog = self.app.activeModalWidget()
                    observed.append((dialog.fileMode(), dialog.testOption(QFileDialog.Option.DontUseNativeDialog)))
                    if not accept:
                        dialog.reject()
                    elif mode == QFileDialog.FileMode.Directory:
                        dialog.setDirectory(str(target))
                        dialog.accept()
                    else:
                        dialog.setDirectory(str(target.parent))
                        dialog.selectFile(str(target))
                        dialog.accept()

                QTimer.singleShot(0, inspect)
                button.click()
                self.assertEqual(observed, [(mode, True)])
                if accept:
                    self.assertEqual(Path(field.text()).resolve(strict=True), target.resolve(strict=True))
                else:
                    self.assertEqual(field.text(), original)

    def test_setup_folder_shortcuts_browse_first_and_delegate_on_request(self):
        from PyQt6.QtWidgets import QPushButton, QTreeView
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        from installer.setup_model import Session
        window = SetupWindow(Bundle(self.root, self.root/'runtime', self.root/'libs', 'a'*40),
                             preferences=self.preferences)
        self.addCleanup(window.close)
        window.controller = self.controller
        window.destination_path = self.root/'installed'
        window.destination_path.mkdir()
        window.controller.session = Session('test', self.root, self.root/'disc.iso')
        window.controller.session.log_path = self.root/'worker.log'
        window.show()
        for button, folder in ((window.open_folder_button, window.destination_path),
                               (window.logs_button, self.root)):
            with patch('installer.setup_dialogs.QDesktopServices.openUrl', return_value=True) as external:
                button.setEnabled(True)
                button.click()
                dialog = self.app.activeModalWidget()
                self.assertEqual(dialog.objectName(), 'folderView')
                tree = dialog.findChild(QTreeView)
                expected = folder.resolve(strict=True)
                self.assertEqual(Path(tree.model().filePath(tree.rootIndex())).resolve(strict=True), expected)
                self.assertTrue(tree.model().isReadOnly())
                external.assert_not_called()
                manager = next(b for b in dialog.findChildren(QPushButton) if b.text() == 'Open in file manager')
                manager.click()
                self.assertEqual(Path(external.call_args.args[0].toLocalFile()).resolve(strict=True), expected)
                dialog.close()
                self.app.processEvents()

    def test_folder_preparation_runs_outside_the_qt_event_loop(self):
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        from PyQt6.QtCore import QTimer
        source=self.root/'own.wbfs';source.write_bytes(b'fixture')
        window=SetupWindow(Bundle(self.root,self.root/'runtime',self.root/'libs','a'*40),preferences=self.preferences)
        window.source.setText(str(source));window.destination.setText(str(self.root/'destination'))
        ticks=[];timer=QTimer();timer.timeout.connect(lambda:ticks.append(window.preparation is not None));timer.start(5)
        def prepare(bundle,destination):time.sleep(.2);return self.root
        with patch('installer.setup_gui.prepare_destination',side_effect=prepare):
            window.install();self.wait(lambda:window.preparation is None)
        self.assertIn(True,ticks);timer.stop()
        if window.controller is not None:self.wait(lambda:not window.controller.busy)
        window.close();window.deleteLater();self.app.processEvents()

    def test_closing_the_window_waits_for_worker_cancellation(self):
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        window=SetupWindow(Bundle(self.root,self.root/'runtime',self.root/'libs','a'*40),preferences=self.preferences)
        window.controller=self.controller
        self.controller.changed.connect(window.render);self.controller.finished.connect(window.finished)
        window.show();self.controller.start(self.root/'cancel.wbfs')
        self.wait(lambda:self.controller.session.progress==50)
        window.close();self.wait(lambda:not self.controller.busy and not window.isVisible())
        self.assertEqual(self.controller.session.status,'cancelled')
        window.deleteLater();self.app.processEvents()

    def test_compact_progress_view_keeps_failure_and_retry_accessible(self):
        from PyQt6.QtWidgets import QBoxLayout,QScrollArea
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        window=SetupWindow(Bundle(self.root,self.root/'runtime',self.root/'libs','a'*40),preferences=self.preferences)
        self.addCleanup(window.close)
        window.source.setText(str(self.root/'fail.wbfs'))
        window.resize(620,520);window.show();self.app.processEvents()
        self.assertEqual(window.columns.direction(),QBoxLayout.Direction.TopToBottom)
        self.assertEqual(window.findChild(QScrollArea).horizontalScrollBar().maximum(),0)
        window.details_button.click()
        self.assertTrue(window.details.isVisible())
        window.controller=self.controller
        self.controller.changed.connect(window.render);self.controller.finished.connect(window.finished)
        self.controller.start(self.root/'fail.wbfs')
        self.assertTrue(window.cancel_button.isVisible())
        self.assertFalse(window.install_button.isVisible())
        self.wait(lambda:not self.controller.busy)
        self.assertEqual(window.stage.property('status'),'failed')
        self.assertIn('Synthetic compilation',window.stage.text())
        self.assertTrue(window.install_button.isVisible() and window.install_button.isEnabled())
        self.assertFalse(window.open_button.isVisible())
        self.assertFalse(window.cancel_button.isVisible())

    def test_language_default_switch_and_restart(self):
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        bundle=Bundle(self.root,self.root/'runtime',self.root/'libs','a'*40)
        self.preferences.setValue('ui/language','invalid')
        window=SetupWindow(bundle,preferences=self.preferences)
        self.assertEqual((window.languages.currentText(),window.install_button.text()),('English','Install'))
        source=str(self.root/'my copy.wbfs')
        window.source.setText(source);window.details_button.click()
        window.languages.setCurrentIndex(window.languages.findData('zh-CN'))
        self.assertEqual(window.install_button.text(),'安装')
        self.assertEqual(window.stage.text(),'可以开始安装')
        self.assertEqual(window.details_button.text(),'收起详情')
        self.assertEqual(window.steps.rows[0][1].text(),'安装工具')
        self.assertEqual(window.source.text(),source)
        window.close()
        reopened=SetupWindow(bundle,preferences=self.preferences)
        self.assertEqual(reopened.languages.currentText(),'简体中文')
        reopened.languages.setCurrentIndex(reopened.languages.findData('en'))
        self.assertEqual(reopened.install_button.text(),'Install')
        reopened.close()

    def test_switch_to_spanish_during_installation_then_cancel(self):
        from installer.setup_gui import SetupWindow
        from installer.setup_layout import Bundle
        window=SetupWindow(Bundle(self.root,self.root/'runtime',self.root/'libs','a'*40),preferences=self.preferences)
        self.addCleanup(window.close)
        window.controller=self.controller
        self.controller.changed.connect(window.render);self.controller.finished.connect(window.finished)
        window.show();window.elapsed.start();self.controller.start(self.root/'cancel.wbfs')
        self.wait(lambda:self.controller.session.progress==50)
        window.languages.setCurrentIndex(window.languages.findData('es'))
        self.assertEqual(window.progress.value(),50)
        self.assertEqual(window.stage.text(),'Compilando el juego')
        self.assertIn('5 de 10 tareas',window.progress_label.text())
        self.assertIn('Tiempo transcurrido',window.time_label.text())
        self.assertEqual(window.steps.rows[3][2].text(),'En curso')
        self.assertTrue(window.cancel_button.isEnabled())
        self.assertFalse(window.source.isEnabled())
        window.cancel_button.click()
        self.wait(lambda:not self.controller.busy)
        self.assertEqual(window.controller.session.status,'cancelled')
        self.assertEqual(window.install_button.text(),'Reintentar')
        self.assertEqual(window.steps.rows[3][2].text(),'Cancelado')


if __name__=='__main__':unittest.main()
