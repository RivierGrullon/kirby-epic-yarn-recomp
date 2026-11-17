# SPDX-License-Identifier: GPL-3.0-only
"""Select this process's project modules in the shared embedded interpreter."""
import os
import sys

_root = os.environ.get('KIRBY_PROJECT_ROOT')
if _root:
    _tools = os.path.join(_root, 'tools')
    if os.path.isdir(_tools):
        sys.path.insert(0, _tools)
