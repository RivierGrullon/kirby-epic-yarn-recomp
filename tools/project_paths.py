# SPDX-License-Identifier: GPL-3.0-only
"""Locate writable project sources from developer and packaged workers."""
import os
from pathlib import Path
import sys


def project_root():
    if getattr(sys, 'frozen', False):
        selected = os.environ.get('KIRBY_PROJECT_ROOT')
        return Path(selected).resolve() if selected else Path(sys.executable).resolve().parents[2]
    return Path(__file__).resolve().parents[1]
