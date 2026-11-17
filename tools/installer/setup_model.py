# SPDX-License-Identifier: GPL-3.0-only
"""Qt-independent validation and presentation of setup worker events."""
import json
from pathlib import Path
import re

STAGES = {'inspect':'Checking your copy', 'prepare':'Preparing installation tools',
          'hash':'Reading your disc image', 'unpack':'Unpacking your archive',
          'extract':'Extracting game data', 'validate':'Verifying game data',
          'translate':'Translating the game', 'compile':'Compiling the game',
          'verify':'Checking game startup', 'launcher':'Preparing the launcher',
          'publish':'Saving your installation', 'select':'Finishing your installation',
          'install':'Installation', 'request':'Setup request'}
PHASES = ('Installation tools', 'Game data', 'Translation', 'Compilation', 'Final checks')


class Session:
    def __init__(self, identifier, root, source):
        self.identifier, self.root, self.source = identifier, Path(root).resolve(), Path(source).absolute()
        self.sequence = 1
        self.pending = b''
        self.status = 'running'
        self.stage = 'inspect'
        self.label = STAGES['inspect']
        self.completed = self.total = None
        self.unit = 'bytes'
        self.error = ''
        self.log_path = None
        self.result = None
        self.terminal = None
        self.phases = ['pending']*5
        self.phase = None

    @property
    def progress(self):
        return None if not self.total else int(self.completed*100/self.total)

    @property
    def progress_text(self):
        if not self.total:
            return 'Working…'
        if self.unit == 'tasks':
            return f'{self.progress}% · {self.completed:,} / {self.total:,} build tasks'
        return f'{self.progress}% · {self.completed/1048576:.1f} / {self.total/1048576:.1f} MiB'

    def feed(self, data):
        self.pending += data
        events = []
        while b'\n' in self.pending:
            line, self.pending = self.pending.split(b'\n', 1)
            if not line or len(line) > 65536:
                raise ValueError('Invalid setup response length')
            event = json.loads(line.decode('utf-8'))
            self.accept(event);events.append(event)
        if len(self.pending) > 65536:
            raise ValueError('Setup response exceeds 64 KiB')
        return events

    def accept(self, event):
        if self.terminal is not None:
            raise ValueError('Setup sent data after completion')
        if (not isinstance(event, dict) or type(event.get('version')) is not int or event['version'] != 1
                or event.get('job_id') != self.identifier or type(event.get('sequence')) is not int
                or event['sequence'] != self.sequence or event.get('stage') not in STAGES
                or event.get('state') not in ('started','progress','complete','failed','cancelled','rejected')):
            raise ValueError('Invalid setup response identity, sequence or stage')
        completed, total = event.get('completed'), event.get('total')
        unit = event.get('unit', 'bytes')
        if ('completed' in event or 'total' in event) and (
                type(completed) is not int or type(total) is not int or not 0 <= completed <= total):
            raise ValueError('Invalid setup progress totals')
        if unit not in ('bytes','tasks'):
            raise ValueError('Unknown setup progress unit')
        detail = event.get('detail', '')
        if not isinstance(detail, str) or len(detail)>256 or '\0' in detail:
            raise ValueError('Invalid setup progress detail')
        error = event.get('error','')
        if not isinstance(error,str) or len(error)>8192 or '\0' in error:
            raise ValueError('Invalid setup error')
        if 'log_path' in event:
            path = Path(event['log_path'])
            if not path.is_absolute() or not path.resolve().is_relative_to(self.root/'local/setup/logs'):
                raise ValueError('Setup log is outside this installation')
            self.log_path = path
        if 'result' in event:
            if (event['stage'],event['state']) != ('install','complete'):
                raise ValueError('Unexpected setup result')
            result = event['result']
            from .platforms import executable_name
            identifier = result.get('installation_id') if isinstance(result,dict) else None
            if (not isinstance(identifier,str) or not re.fullmatch('[a-f0-9]{64}',identifier)
                    or result.get('committed') is not True or not isinstance(result.get('source'),str)
                    or Path(result['source']).resolve() != self.source.resolve()
                    or result.get('executable') != str(self.root/'local/setup/installations'/identifier/executable_name())
                    or not isinstance(result.get('data_root'),str) or not Path(result['data_root']).is_absolute()):
                raise ValueError('Setup did not commit the selected installation')
        elif (event['stage'],event['state']) == ('install','complete'):
            raise ValueError('Setup completion is missing its installation')
        if event['stage']=='install' and event['state'] in ('complete','failed','cancelled'):
            if event['state']!='complete' and not error:
                raise ValueError('Setup failure is missing its explanation')
            self.terminal = event
        self.sequence += 1
        self.stage, self.label = event['stage'], detail or STAGES[event['stage']]
        self.completed, self.total, self.unit = completed, total, unit
        self.error = error
        if self.stage in ('inspect','request'):
            return
        if self.stage=='install' and event['state'] in ('failed','cancelled'):
            return  # Keep the actual failing phase; do not complete it on an error.
        group = (0 if self.stage=='prepare' else 1 if self.stage in ('inspect','hash','unpack','extract','validate')
                 else 2 if self.stage=='translate' else 3 if self.stage=='compile' else 4)
        if self.phase is not None and self.phase != group and self.phases[self.phase]=='running':
            self.phases[self.phase]='done'
        self.phase = group
        self.phases[group] = 'done' if event['state']=='complete' else 'running'

    def finish(self, exit_code, crashed=False):
        terminal = self.terminal
        if self.pending.strip():
            self.error = 'Setup stopped with an incomplete response'
        elif terminal and terminal['state']=='complete' and exit_code==0 and not crashed:
            self.status = 'ready';self.result = terminal['result'];self.phases = ['done']*5
            self.label = 'Your installation is ready';return
        elif terminal and terminal['state']=='cancelled' and exit_code==2:
            self.status = 'cancelled';self.label = 'Installation cancelled';return
        self.status = 'failed';self.result = None
        self.error = self.error or 'Setup stopped before finishing. Open the details and try again.'
        if self.phase is not None:self.phases[self.phase]='failed'
