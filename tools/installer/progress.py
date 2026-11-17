# SPDX-License-Identifier: GPL-3.0-only
"""Recognize measured tool progress without inventing a whole-install percentage."""
import re


class ToolProgress:
    def __init__(self, stage, emit):
        self.stage, self.emit = stage, emit
        self.pending = b''
        self.last = None

    def __call__(self, chunk):
        self.pending += chunk
        lines = re.split(rb'[\r\n]', self.pending)
        self.pending = lines.pop()[-8192:]
        for line in lines:
            match = re.match(rb'^\[(\d{1,9})/(\d{1,9})\]', line)
            if match is None:
                continue
            completed, total = map(int, match.groups())
            if not 0 <= completed <= total or not total or (completed, total) == self.last:
                continue
            self.last = completed, total
            self.emit(dict(stage=self.stage, state='progress', completed=completed, total=total, unit='tasks'))
