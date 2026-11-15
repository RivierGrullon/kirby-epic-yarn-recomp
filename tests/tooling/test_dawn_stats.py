# SPDX-License-Identifier: GPL-3.0-only
import csv
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import dawn_stats


class DawnStatsTests(unittest.TestCase):
    def read(self, rows):
        stream = io.StringIO()
        writer = csv.writer(stream)
        writer.writerow(dawn_stats.FIELDS)
        writer.writerows(rows)
        stream.seek(0)
        return dawn_stats.read_events(stream)

    def test_cross_thread_overlap_uses_union_and_infers_guest_before_filtering(self):
        # Completion order differs from start order. Two submits overlap each
        # other; the union inside the creation call is 4 ms, not 6 ms.
        events = self.read([
            (1, 'FrameWorkerWait', 0, 0, 0, 100, 0, 0, 0, 0),
            (2, 'QueueSubmit', 1_000_000, 4_000_000, 400, 200, 0, 0, 1, 0),
            (3, 'CreateTexture', 0, 5_000_000, 900, 100, 64, 64, 0, 1),
            (4, 'QueueSubmit', 2_000_000, 6_000_000, 600, 300, 0, 0, 1, 0),
            (5, 'CreateView', 6_000_000, 7_000_000, 200, 100, 0, 0, 0, 1),
        ])
        report = dawn_stats.summarize(events)
        self.assertEqual(report['guest_tid'], 100)
        self.assertEqual(report['guest_resource_calls'], 2)
        self.assertEqual(report['resource_calls_overlapping_submit'], 1)
        self.assertEqual(report['largest_overlaps'][0]['submit_overlap_ms'], 4)
        self.assertEqual(report['operations'][0]['wall_ms']['max'], 5)
        filtered = dawn_stats.summarize(events, start_ns=6_000_000, end_ns=7_000_000)
        self.assertEqual(filtered['guest_tid'], 100)
        self.assertEqual(filtered['events_selected'], 1)
        self.assertEqual(filtered['guest_resource_calls'], 1)
        self.assertEqual(filtered['resource_calls_overlapping_submit'], 0)

    def test_same_thread_submit_does_not_count_as_device_contention(self):
        events = self.read([
            (1, 'QueueSubmit', 1, 9, 2, 10, 0, 0, 1, 0),
            (2, 'WriteBuffer', 0, 10, 3, 10, 0, 0, 16, 0),
        ])
        report = dawn_stats.summarize(events, guest_tid=10)
        self.assertEqual(report['resource_calls_overlapping_submit'], 0)

    def test_submit_crossing_selection_boundary_is_included_in_overlap(self):
        events = self.read([
            (1, 'CreateTexture', 3, 5, 1, 10, 1, 1, 0, 1),
            (2, 'QueueSubmit', 0, 8, 2, 20, 0, 0, 1, 0),
        ])
        report = dawn_stats.summarize(events, start_ns=3, end_ns=5, guest_tid=10)
        self.assertEqual(report['events_selected'], 1)
        self.assertEqual(report['largest_overlaps'][0]['submit_overlap_ms'], 2/1e6)

    def test_invalid_or_truncated_rows_are_rejected(self):
        valid = [1, 'CreateTexture', 1, 10, 1, 10, 1, 1, 0, 1]
        for index, value in ((0, 2), (2, -1), (3, 0), (4, -1), (5, 0)):
            row = valid.copy(); row[index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.read([row])
        for row in (valid[:-1], [*valid, 'extra']):
            with self.assertRaises(ValueError):
                self.read([row])
        with self.assertRaises(ValueError):
            dawn_stats.read_events(io.StringIO('sequence,sequence\n1,1\n'))

    def test_ambiguous_guest_requires_explicit_selection(self):
        events = self.read([
            (1, 'FrameWorkerWait', 0, 1, 0, 10, 0, 0, 0, 0),
            (2, 'FrameWorkerWait', 2, 3, 0, 20, 0, 0, 0, 0),
        ])
        with self.assertRaisesRegex(ValueError, 'guest-tid'):
            dawn_stats.summarize(events)
        self.assertEqual(dawn_stats.summarize(events, guest_tid=10)['guest_tid'], 10)


if __name__ == '__main__':
    unittest.main()
