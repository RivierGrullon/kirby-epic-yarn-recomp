# SPDX-License-Identifier: GPL-3.0-only
"""Own a build container beyond the lifetime of its Podman client process."""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import uuid
from container_engine import TOOLCHAIN_IMAGE, image_name, select_engine, run_command

from .jobs import run_process
from .platforms import open_read, is_link



class CleanupError(ValueError):
    pass


@dataclass(frozen=True)
class ContainerResult:
    image_id:str
    output:bytes


def _image_id(value):
    if not isinstance(value,str) or not re.fullmatch(r'(sha256:)?[a-f0-9]{64}',value):
        raise ValueError('Toolchain requires an immutable container image ID')
    return value.removeprefix('sha256:')


def toolchain_image(job):
    """Resolve the local build tag once; callers can reuse the immutable result."""
    output=run_process(job,[select_engine(),'image','inspect',image_name(),'--format','{{.Id}}'],
                       capture_limit=256,timeout=10)
    return _image_id(output.decode('ascii').strip())


def _control(job,arguments,engine='podman'):
    # Cleanup must run after cancellation and even after the primary log is full.
    return run_process(job,[engine,*arguments],capture_limit=65536,timeout=3,
                       cancellable=False,log_output=False)


def _cleanup(job,directory,receipt):
    engine=receipt.get('engine','podman')
    filters=['--filter','label=io.kirby.setup-job='+receipt['job_id'],
             '--filter','label=io.kirby.setup-token='+receipt['token']]
    try:
        query=['ps','--all',*filters,'--format','{{.ID}}']
        if engine=='docker':
            owned=_control(job,query,engine).decode('ascii').split()
            if any(not re.fullmatch(r'[a-f0-9]{12,64}', identifier) for identifier in owned):
                raise ValueError('Invalid owned container ID from Docker')
            if owned:
                try:
                    _control(job,['rm','--force',*owned],engine)
                except ValueError:
                    if _control(job,query,engine).strip():
                        raise
        else:
            _control(job,['rm','--force','--ignore','--time','1',*filters],engine)
        remaining=_control(job,query,engine)
        if remaining.strip():
            raise ValueError('The owned setup container is still present')
    except (OSError,ValueError) as error:
        raise CleanupError(f'Could not verify setup container cleanup; ownership receipt: '
                           f'{directory/"ownership.json"}. {error}') from error
    shutil.rmtree(directory)


def recover_containers(job):
    """Recover recorded containers only while holding the setup workspace lock."""
    if job.lock_fd is None:
        raise ValueError('Container recovery requires the active setup workspace lock')
    for directory in sorted((job.workspace/'staging').glob('container-*')):
        try:
            if is_link(directory) or not directory.is_dir():
                raise ValueError('Container receipt directory is not a regular directory')
            fd=open_read(directory/'ownership.json')
            with os.fdopen(fd,'rb') as stream:
                info=os.fstat(stream.fileno())
                if not stat.S_ISREG(info.st_mode) or info.st_size>4096:
                    raise ValueError('Container receipt is not bounded regular metadata')
                data=stream.read(4097)
            receipt=json.loads(data)
            if (not isinstance(receipt,dict) or type(receipt.get('version')) is not int or receipt['version']!=1
                    or receipt.get('engine','podman') not in ('podman','docker')
                    or not isinstance(receipt.get('job_id'),str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,80}',receipt['job_id'])
                    or not isinstance(receipt.get('token'),str) or not re.fullmatch(r'[a-f0-9]{32}',receipt['token'])
                    or receipt.get('name')!='kirby-setup-'+receipt['token']):
                raise ValueError('Invalid container ownership receipt')
            _image_id(receipt.get('image_id'))
        except (OSError,ValueError) as error:
            raise CleanupError(f'Cannot recover container receipt {directory}: {error}') from error
        _cleanup(job,directory,receipt)


def run_container(job,root,command,*,image_id=None,capture_limit=0):
    """Run one managed tool, then remove and verify only this ownership token.

    A CID file alone cannot be trusted to identify a container for removal.
    Two labels select the job and its unpredictable per-run token instead.
    If cleanup cannot be verified, retain the receipt and report a failure.
    """
    job.check_cancel()
    if not command:
        raise ValueError('A managed container command is required')
    image=_image_id(image_id) if image_id is not None else toolchain_image(job)
    token=uuid.uuid4().hex
    engine=select_engine()
    directory=Path(tempfile.mkdtemp(prefix='container-',dir=job.workspace/'staging'))
    receipt=dict(version=1,job_id=job.identifier,token=token,image_id=image,name='kirby-setup-'+token,engine=engine)
    try:
        (directory/'ownership.json').write_text(json.dumps(receipt)+'\n')
    except BaseException:
        shutil.rmtree(directory)
        raise
    try:
        arguments=run_command(root,engine,image,command,managed=(directory/'container.cid',job.identifier,token))
        output=run_process(job,arguments,capture_limit=capture_limit)
        return ContainerResult(image,output)
    finally:
        _cleanup(job,directory,receipt)
