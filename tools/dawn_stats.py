# SPDX-License-Identifier: GPL-3.0-only
"""Summarize opt-in Linux WebGPU call timings, not GPU execution or display FPS."""
import argparse
import bisect
import collections
import csv
from decimal import Decimal, InvalidOperation
import json
from pathlib import Path
import re

from frame_stats import distribution

FIELDS = ('sequence', 'operation', 'start_ns', 'end_ns', 'cpu_ns', 'tid',
          'width', 'height', 'amount', 'result')


def read_events(stream):
    reader = csv.DictReader(stream)
    if reader.fieldnames != list(FIELDS):
        raise ValueError('Unsupported or duplicate resource trace columns')
    events = []
    for expected, raw in enumerate(reader, 1):
        if set(raw) != set(FIELDS) or any(value is None for value in raw.values()):
            raise ValueError(f'Truncated or extra fields at event {expected}')
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9]*', raw['operation']):
            raise ValueError(f'Invalid operation at event {expected}')
        row = {'operation': raw['operation']}
        for name in FIELDS:
            if name == 'operation':
                continue
            if not re.fullmatch(r'[0-9]+', raw[name]):
                raise ValueError(f'Invalid {name} at event {expected}')
            row[name] = int(raw[name])
        if row['sequence'] != expected or row['end_ns'] < row['start_ns'] or not row['tid']:
            raise ValueError(f'Invalid sequence, interval or thread at event {expected}')
        events.append(row)
    if not events:
        raise ValueError('Resource trace contains no events')
    return events


def overlap_ns(start, end, intervals):
    """Measure the union, including nested submissions from different threads."""
    total = 0
    cursor = start
    for before, after in intervals:
        before, after = max(start, before), min(end, after)
        if after > max(cursor, before):
            total += after - max(cursor, before)
            cursor = after
    return total


def summarize(events, *, start_ns=0, end_ns=None, guest_tid=None):
    if start_ns < 0 or (end_ns is not None and end_ns <= start_ns):
        raise ValueError('The selected interval must be positive and ordered')
    if guest_tid is None:
        candidates = {row['tid'] for row in events if row['operation'] == 'FrameWorkerWait'}
        if len(candidates) != 1:
            raise ValueError('Specify --guest-tid when FrameWorkerWait has no unique thread')
        guest_tid = candidates.pop()
    if guest_tid <= 0:
        raise ValueError('Guest thread must be positive')
    selected = [row for row in events if start_ns <= row['start_ns'] and
                (end_ns is None or row['end_ns'] <= end_ns)]
    if not selected:
        raise ValueError('No complete events in the selected interval')
    groups = collections.defaultdict(list)
    for row in selected:
        groups[row['operation'], row['tid']].append(row)
    operations = []
    for (operation, tid), rows in groups.items():
        wall = [(row['end_ns'] - row['start_ns'])/1e6 for row in rows]
        operations.append(dict(operation=operation, tid=tid, role='guest' if tid == guest_tid else 'other',
                               count=len(rows), wall_ms=distribution(wall),
                               cpu_ms=distribution([row['cpu_ns']/1e6 for row in rows])))
    # Include submissions crossing the selection boundary: they may overlap a
    # fully selected resource call. Completion order is not start order.
    submits = sorted((row['start_ns'], row['end_ns']) for row in events
                     if row['operation'] == 'QueueSubmit' and row['tid'] != guest_tid)
    starts = [before for before, _ in submits]
    prefix_ends = []
    for _, after in submits:
        prefix_ends.append(max(prefix_ends[-1] if prefix_ends else 0, after))
    resources = [row for row in selected if row['tid'] == guest_tid and
                 row['operation'].startswith(('Create', 'Write'))]
    overlaps = []
    for row in resources:
        before, after = row['start_ns'], row['end_ns']
        low = bisect.bisect_right(prefix_ends, before)
        high = bisect.bisect_left(starts, after)
        duration = overlap_ns(before, after, submits[low:high])
        if duration:
            overlaps.append(dict(sequence=row['sequence'], operation=row['operation'],
                                 start_ns=before, end_ns=after, cpu_ms=row['cpu_ns']/1e6,
                                 wall_ms=(after-before)/1e6, submit_overlap_ms=duration/1e6))
    return dict(measurement='Host API wall and thread CPU time; not GPU execution or displayed FPS',
                limitation='Temporal overlap does not prove contention on a specific Dawn mutex; logging adds overhead',
                events_total=len(events), events_selected=len(selected), guest_tid=guest_tid,
                start_ns=start_ns, end_ns=end_ns,
                operations=sorted(operations, key=lambda item: item['wall_ms']['max'], reverse=True),
                guest_resource_calls=len(resources), resource_calls_overlapping_submit=len(overlaps),
                largest_overlaps=sorted(overlaps, key=lambda item: item['wall_ms'], reverse=True)[:20])


def seconds_ns(value):
    try:
        seconds = Decimal(value)
        if not seconds.is_finite() or seconds < 0:
            raise ValueError()
        return int(seconds*1_000_000_000)
    except (ValueError, InvalidOperation, OverflowError) as error:
        raise argparse.ArgumentTypeError('Expected finite nonnegative SDL seconds') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--start', type=seconds_ns, default=0, help='SDL_GetTicksNS epoch, in seconds')
    parser.add_argument('--end', type=seconds_ns, help='Include only fully contained resource calls')
    parser.add_argument('--guest-tid', type=int)
    args = parser.parse_args()
    try:
        with args.trace.open(newline='') as stream:
            report = summarize(read_events(stream), start_ns=args.start, end_ns=args.end,
                               guest_tid=args.guest_tid)
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(1, f'Resource trace: {error}\n')


if __name__ == '__main__':
    main()
