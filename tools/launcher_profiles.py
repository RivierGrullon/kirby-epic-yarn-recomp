# SPDX-License-Identifier: GPL-3.0-only
"""Ship the launch profile beside each installer, with its exact source revision."""
import json
from pathlib import Path


def write_profile(root, output, revision):
    value=json.loads((Path(root)/'recomplauncher.json').read_text(encoding='utf-8'))
    value['prepare']['version']=revision
    (Path(output)/'recomplauncher.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
