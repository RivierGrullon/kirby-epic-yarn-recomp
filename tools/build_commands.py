# SPDX-License-Identifier: GPL-3.0-only
"""Arguments shared by developer builds and staged installations."""


def translate_command(dol, jobs, *, stage=None):
    command = ['tools/kirby.py', 'translate', '--dol', str(dol), '--threads', str(jobs)]
    if stage is not None:
        command.extend(['--stage-root', str(stage)])
    return command


def native_command(target, jobs, translated_jobs, *, stage=None, platform_args=()):
    command = ['tools/build_native.py', '--target', target, '--jobs', str(jobs),
               '--translated-jobs', str(translated_jobs)]
    if stage is not None:
        command.extend(['--stage-root', str(stage)])
    return [*command, *platform_args]
