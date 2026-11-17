# SPDX-License-Identifier: GPL-3.0-only
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SUPERVISOR=Path(__file__).resolve().parents[2]/'tools/installer/setup_supervisor.py'


@unittest.skipUnless(sys.platform=='linux','Linux subreaper ownership')
class SetupSupervisorTests(unittest.TestCase):
    def test_crashed_worker_cannot_leave_a_detached_compiler_alive(self):
        with tempfile.TemporaryDirectory() as temporary:
            worker=Path(temporary)/'crash.py'
            worker.write_text('import subprocess,sys\n'
                'p=subprocess.Popen([sys.executable,"-c","import time;time.sleep(60)"],start_new_session=True)\n'
                'print(p.pid,flush=True)\nsys.exit(9)\n')
            result=subprocess.run([sys.executable,str(SUPERVISOR),'--',sys.executable,str(worker)],
                                  capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,9,result.stderr)
            self.assertFalse((Path('/proc')/result.stdout.strip()).exists())

    def test_supervisor_preserves_worker_stdin_stdout_and_exit_code(self):
        result=subprocess.run([sys.executable,str(SUPERVISOR),'--',sys.executable,'-c',
                               'import sys;print(input());sys.exit(2)'],input='literal $ source\n',
                              capture_output=True,text=True,timeout=10)
        self.assertEqual(result.stdout,'literal $ source\n');self.assertEqual(result.returncode,2)


if __name__=='__main__':unittest.main()
