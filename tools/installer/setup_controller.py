# SPDX-License-Identifier: GPL-3.0-only
"""Asynchronous Qt transport for the existing cancellable setup worker."""
import json
import os
from pathlib import Path
import signal
import uuid

from PyQt6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, pyqtSignal
from .setup_model import Session


class Controller(QObject):
    changed = pyqtSignal()
    finished = pyqtSignal(str)
    output = pyqtSignal(str)

    def __init__(self, root, worker_command, parent=None, grace_ms=20000):
        super().__init__(parent)
        self.root, self.worker_command = Path(root).resolve(), list(map(str,worker_command))
        self.session = None
        self.busy = False
        self.cancelling = False
        self.protocol_error = ''
        self.stderr = ''
        self.owned = {}
        self.process = QProcess(self)
        self.process.started.connect(self._started)
        self.process.readyReadStandardOutput.connect(self._read)
        self.process.readyReadStandardError.connect(self._stderr)
        self.process.finished.connect(self._done)
        self.process.errorOccurred.connect(self._error)
        self.grace = QTimer(self);self.grace.setSingleShot(True);self.grace.setInterval(grace_ms)
        self.grace.timeout.connect(self._terminate)
        self.force = QTimer(self);self.force.setSingleShot(True);self.force.setInterval(3000)
        self.force.timeout.connect(self.process.kill)

    def start(self, source):
        if self.busy:raise ValueError('An installation is already running')
        self.root.mkdir(parents=True,exist_ok=True)
        self.session = Session(uuid.uuid4().hex,self.root,source)
        self.busy = True;self.cancelling = False;self.protocol_error = '';self.stderr = '';self.owned = {}
        environment=QProcessEnvironment.systemEnvironment()
        environment.insert('KIRBY_PROJECT_ROOT',str(self.root))
        self.process.setProcessEnvironment(environment)
        self.process.setWorkingDirectory(str(self.root))
        self.process.setProgram(self.worker_command[0])
        self.process.setArguments([*self.worker_command[1:],'--workspace',str(self.root/'local/setup')])
        self.changed.emit();self.process.start()

    def _write(self, operation):
        value=dict(version=1,id=self.session.identifier,operation=operation)
        if operation=='install':value['source']=str(self.session.source)
        self.process.write((json.dumps(value)+'\n').encode('utf-8'))

    def _started(self):
        self._write('install')
        if self.cancelling:self._write('cancel')

    def _read(self):
        data=bytes(self.process.readAllStandardOutput())
        if not data or self.protocol_error:return
        try:
            for event in self.session.feed(data):
                self.output.emit(event.get('detail') or event.get('error') or
                                 self.session.label+' · '+event['state'])
            self.changed.emit()
        except (ValueError,UnicodeError,KeyError,TypeError,RecursionError) as error:
            self.protocol_error='Invalid setup response: '+str(error)
            self.cancel()

    def _stderr(self):
        text=bytes(self.process.readAllStandardError()).decode('utf-8',errors='replace')
        self.stderr=(self.stderr+text)[-8192:]
        self.output.emit(text.rstrip())

    def _error(self, error):
        if error==QProcess.ProcessError.FailedToStart:
            self.protocol_error='Could not start the setup worker: '+self.process.errorString()
            self._done(-1,QProcess.ExitStatus.CrashExit)

    def cancel(self):
        if not self.busy or self.cancelling:return
        self.cancelling=True
        if self.process.state()==QProcess.ProcessState.Running:self._write('cancel')
        self.grace.start();self.changed.emit()

    def _terminate(self):
        # The worker normally cleans its owned processes after the JSON cancel.
        # Remember Linux descendants before escalation can orphan them.
        if os.name!='nt':
            def visit(pid):
                try:
                    children=(Path('/proc')/str(pid)/'task'/str(pid)/'children').read_text().split()
                    for child in children:
                        child=int(child)
                        path=Path('/proc')/str(child)
                        if path.stat().st_uid!=os.getuid():continue
                        self.owned[child]=path.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
                        visit(child)
                except (OSError,ValueError,IndexError):pass
            visit(int(self.process.processId()))
            self._signal_owned(signal.SIGTERM)
        self.process.terminate();self.force.start()

    def _signal_owned(self, number):
        for pid,started in self.owned.items():
            try:
                current=(Path('/proc')/str(pid)/'stat').read_text().rsplit(')',1)[1].split()[19]
                if current==started:
                    if os.getpgid(pid)==pid:os.killpg(pid,number)
                    else:os.kill(pid,number)
            except (OSError,ValueError,IndexError):pass

    def _done(self, code, status):
        if not self.busy:return
        self.grace.stop();self.force.stop();self._read();self._stderr()
        self.grace.stop();self.force.stop()
        if os.name!='nt':self._signal_owned(signal.SIGKILL)
        self.session.finish(code,status==QProcess.ExitStatus.CrashExit)
        if self.protocol_error:
            self.session.status='failed';self.session.error=self.protocol_error;self.session.result=None
        elif self.cancelling and self.session.status=='failed' and not self.session.terminal:
            self.session.status='cancelled';self.session.label='Installation cancelled'
        self.busy=False;self.changed.emit();self.finished.emit(self.session.status)
