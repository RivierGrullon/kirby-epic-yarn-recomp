# SPDX-License-Identifier: GPL-3.0-only
"""A process-backed engine fixture: clients can die while containers remain."""
import json
import os
from pathlib import Path
import sys
import time

path=Path(os.environ['KIRBY_TEST_PODMAN_STATE'])
state=json.loads(path.read_text())
args=sys.argv[1:]
docker=Path(sys.argv[0]).name=='docker'
if docker and any(arg in args for arg in ('--ignore','--time','--userns=keep-id')):
    raise RuntimeError('Podman-only option sent to Docker')
image='a'*64
owned='b'*64


def save():
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(state))
    temporary.replace(path)


def matching():
    filters=[args[i+1][6:].split('=',1) for i,a in enumerate(args) if a=='--filter' and args[i+1].startswith('label=')]
    if len(filters)!=2:
        raise RuntimeError('Managed cleanup requires both ownership labels')
    return [key for key,value in state['containers'].items()
            if all(value['labels'].get(k)==v for k,v in filters)]


if args[:2]==['image','inspect']:
    print(image)
elif args[:2]==['image','exists']:
    sys.exit(0)
elif args[0]=='run':
    labels=dict(args[i+1].split('=',1) for i,a in enumerate(args) if a=='--label')
    command=args[args.index(image)+1:]
    state['containers'][owned]=dict(labels=labels,command=command)
    save()
    if not state.get('missing_cid'):
        Path(args[args.index('--cidfile')+1]).write_text(('c'*64 if state.get('wrong_cid') else owned)+'\n')
    print(json.dumps(command),flush=True)
    if command[0]=='hold':
        while True: time.sleep(0.1)
    sys.exit(7 if command[0]=='fail' else 0)
elif args[0]=='rm':
    assert '--all' not in args and '--latest' not in args
    if state.get('fail_cleanup'):
        print('Synthetic engine cleanup unavailable',file=sys.stderr)
        sys.exit(125)
    selected = args[2:] if docker else matching()
    if docker:
        assert args[1]=='--force'
        assert all(key in state['containers'] and state['containers'][key]['labels'].get('io.kirby.setup-token') for key in selected)
    for key in selected:
        del state['containers'][key]
    save()
elif args[0]=='ps':
    for key in matching(): print(key)
else:
    raise RuntimeError('Unexpected engine operation')
