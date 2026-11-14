# SPDX-License-Identifier: GPL-3.0-only
"""Translate, build and verify an owned source before selecting a native copy."""
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile

import build_info
from build_layout import BuildLayout
import kirby
from .container import recover_containers, run_container, toolchain_image
from .inputs import build_keys, generated_digest, host_compile_jobs
from .installations import InstallationStore, VERIFICATION, canonical, file_digest, read_json
from .jobs import run_process
from .verification import verify_native


class BuildTools:
    def __init__(self, job, root):
        self.job, self.root = job, root
        self.image_id = None

    def bootstrap(self):
        recover_containers(self.job)
        run_process(self.job, [sys.executable, self.root/'tools/bootstrap.py'], cwd=self.root)

    def image(self):
        self.image_id = toolchain_image(self.job)
        return self.image_id

    def identity(self):
        record = build_info.collect(self.root, compiler='pending', build_type='Release')
        return {key: record[key] for key in ('source_revision', 'source_modified', 'source_sha256')}

    def jobs(self):
        return host_compile_jobs()

    def run(self, arguments):
        return run_container(self.job, self.root, arguments, image_id=self.image_id)

    def verify(self, executable, data, identity):
        return verify_native(self.job, self.root, executable, data, identity)


def _receipt(path):
    try:
        return read_json(path)
    except (OSError, ValueError):
        return None


def _write_receipt(path, value):
    fd, name = tempfile.mkstemp(prefix='.receipt-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(canonical(value)+b'\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def _copy(source, destination, job):
    expected = file_digest(source, job.check_cancel)
    shutil.copyfile(source, destination, follow_symlinks=False)
    if destination.is_symlink() or file_digest(destination, job.check_cancel) != expected:
        raise ValueError('Build input or output changed while copying')
    job.check_cancel()


def _native_files(directory, job):
    return {name: dict(zip(('sha256', 'size'), file_digest(directory/name, job.check_cancel)))
            for name in ('WiiCompiled', 'dsp_coef.bin')}


def execute_install(job, root, source, *, services=None):
    """A failed/cancelled stage never changes the previous executable selection.

    Mutable compiler outputs are reusable caches. Only independent copies that
    pass the native probes can enter the immutable installation store.
    """
    root = Path(root).resolve()
    if job.lock_fd is None:
        raise ValueError('Native installation requires the setup workspace lock')
    data = Path(source['data_root']).resolve()
    if source.get('dol_sha1') != kirby.EXPECTED_SHA1:
        raise ValueError('Installation requires the verified RK5E01 Rev 0 DOL')
    tools = services if services is not None else BuildTools(job, root)
    job.emit(dict(stage='prepare', state='started'))
    tools.bootstrap()
    image = tools.image()
    identity = tools.identity()
    keys = build_keys(root, identity, image, job.check_cancel)
    layout = BuildLayout(root, root/'local/setup/builds'/('cache-'+keys['cache']))
    layout.ensure()
    copied_dol = layout.metadata/'main.dol'
    _copy(data/'sys/main.dol', copied_dol, job)
    if hashlib.sha1(copied_dol.read_bytes()).hexdigest() != kirby.EXPECTED_SHA1:
        raise ValueError('Selected DOL changed after source inspection')
    translation_receipt = layout.metadata/'translation.json'
    previous = _receipt(translation_receipt)
    translation_reused = (isinstance(previous, dict) and previous.get('key') == keys['translation']
                          and previous.get('generated_sha256') == generated_digest(layout.generated, job.check_cancel))
    jobs = tools.jobs()
    relative = str(layout.stage.relative_to(root))
    if not translation_reused:
        if shutil.disk_usage(layout.stage).free < 6*1024**3:
            raise ValueError('Native preparation requires at least 6 GiB of free disk space')
        job.emit(dict(stage='translate', state='started'))
        tools.run(['python3', 'tools/kirby.py', 'translate', '--stage-root', relative,
                   '--dol', str(copied_dol.relative_to(root)), '--threads', str(jobs)])
        if not any(layout.generated.iterdir()):
            raise ValueError('Translation did not generate native sources')
        _write_receipt(translation_receipt, dict(key=keys['translation'],
                       generated_sha256=generated_digest(layout.generated, job.check_cancel)))
    job.emit(dict(stage='translate', state='complete', reused=translation_reused))
    native_receipt = layout.metadata/'native.json'
    previous = _receipt(native_receipt)
    build_reused = False
    if translation_reused and isinstance(previous, dict) and previous.get('key') == keys['native']:
        try:
            build_reused = previous.get('files') == _native_files(layout.build, job)
        except OSError:
            pass
    if not build_reused:
        # An interrupted compiler must not leave a valid receipt from an older build.
        native_receipt.unlink(missing_ok=True)
        job.emit(dict(stage='compile', state='started', jobs=jobs))
        tools.run(['python3', 'tools/build_native.py', '--stage-root', relative,
                   '--jobs', str(jobs), '--translated-jobs', str(jobs)])
    job.emit(dict(stage='compile', state='complete', reused=build_reused))
    if tools.identity() != identity:
        raise ValueError('Project sources changed during compilation; retry with the saved build cache')
    files = _native_files(layout.build, job)
    with tempfile.TemporaryDirectory(prefix='native-candidate-', dir=job.workspace/'staging') as temporary:
        candidate = Path(temporary)
        for name in files:
            _copy(layout.build/name, candidate/name, job)
        (candidate/'WiiCompiled').chmod(0o500)
        build = tools.verify(candidate/'WiiCompiled', data, identity)
        job.check_cancel()
        if tools.identity() != identity:
            raise ValueError('Project sources changed during native verification; retry installation')
        _write_receipt(native_receipt, dict(key=keys['native'], files=files))
        manifest = dict(version=1, game_id='RK5E01', revision=0, dol_sha1=kirby.EXPECTED_SHA1,
                        data_root=str(data), project_sha256=identity['source_sha256'],
                        translation_key=keys['translation'], native_key=keys['native'],
                        toolchain_image_id=image, build=build, source=source,
                        executable=dict(file='WiiCompiled', **files['WiiCompiled']),
                        resources=[dict(file='dsp_coef.bin', **files['dsp_coef.bin'])],
                        verification=sorted(VERIFICATION))
        job.emit(dict(stage='publish', state='started'))
        store = InstallationStore(root)
        identifier = store.publish(candidate, manifest, check_cancel=job.check_cancel)
    job.emit(dict(stage='select', state='started'))
    store.activate(identifier, check_cancel=job.check_cancel)
    job.committed = True
    return dict(installation_id=identifier, executable=str(store.executable(identifier)),
                data_root=str(data), build_reused=build_reused, translation_reused=translation_reused,
                committed=True)
