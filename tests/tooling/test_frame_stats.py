"""Synthetic timing fixtures; no game, graphics driver or native executable."""
import csv
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import frame_stats


FIELDS = ("sequence,vi_field,start_ns,seal_start_ns,seal_end_ns,wait_end_ns,"
          "finish_ns,surface_presents,readbacks,validation_bytes,draws,queued_pipelines").split(",")


def fixture():
    # Intervals: 20 ms, then 40 ms. The long interval contains 15 ms of
    # previous-row worker wait and 21 ms outside PollVideo.
    return [
        [1, 100, 1000000000, 1001000000, 1002000000, 1004000000, 1006000000, 1, 0, 10, 2, 0],
        [2, 101, 1020000000, 1021000000, 1022000000, 1037000000, 1039000000, 2, 0, 30, 5, 2],
        [3, 103, 1060000000, 1061000000, 1062000000, 1064000000, 1066000000, 3, 1, 60, 9, 0],
    ]


def load(rows):
    stream = io.StringIO()
    writer = csv.writer(stream)
    writer.writerow(FIELDS)
    writer.writerows(rows)
    stream.seek(0)
    return frame_stats.read_samples(stream)


class FrameStatsTests(unittest.TestCase):
    def test_rates_percentiles_and_long_interval_attribution(self):
        report = frame_stats.summarize(load(fixture()))
        self.assertEqual(report["samples"], 3)
        self.assertAlmostEqual(report["submissions_per_second"], 2 / 0.06)
        self.assertEqual(report["interval_ms"]["p50"], 30)
        self.assertEqual(report["interval_ms"]["p95"], 39)
        self.assertEqual(report["counter_deltas"], {
            "vi_field": 3, "surface_presents": 2, "readbacks": 1,
            "validation_bytes": 50, "draws": 7})
        self.assertEqual(report["queued_pipelines_peak"], 2)
        self.assertEqual(report["intervals_over_threshold"], 1)
        longest = report["longest_intervals"][0]
        self.assertEqual(longest["sequence"], 3)
        self.assertEqual(longest["previous_worker_wait_ms"], 15)
        self.assertEqual(longest["between_submissions_ms"], 21)
        self.assertEqual(longest["vi_fields"], 2)
        self.assertEqual(longest["readbacks_since_previous_sample"], 1)

    def test_time_window_excludes_transitions_before_and_after_it(self):
        report = frame_stats.summarize(load(fixture()), start=1.02, end=1.07)
        self.assertEqual(report["samples"], 2)
        self.assertEqual(report["first_sequence"], 2)
        self.assertEqual(report["last_sequence"], 3)
        self.assertAlmostEqual(report["submissions_per_second"], 25)
        self.assertEqual(report["interval_ms"]["max"], 40)

    def test_rejects_partial_or_noninteger_records_instead_of_hiding_stalls(self):
        for tail in ([4, 104], fixture()[0][:4] + ["nan"] + fixture()[0][5:]):
            with self.subTest(tail=tail), self.assertRaisesRegex(ValueError, "line 5"):
                load(fixture() + [tail])

    def test_rejects_nonmonotonic_timestamps_counters_and_missing_rows(self):
        for column, value in ((0, 9), (2, 999000000), (4, 1020000000), (7, 0), (10, 1)):
            rows = fixture()
            rows[1][column] = value
            with self.subTest(column=column), self.assertRaises(ValueError):
                load(rows)

    def test_requires_two_samples_and_a_valid_window(self):
        rows = load(fixture())
        for kwargs in ({"start": 1.05}, {"start": 2, "end": 1},
                       {"start": -1}, {"start": float("nan")},
                       {"threshold_ms": 0}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                frame_stats.summarize(rows, **kwargs)

    def test_rejects_missing_or_duplicate_columns(self):
        for header in ("sequence,start_ns\n", ",".join(FIELDS + ["sequence"]) + "\n"):
            with self.subTest(header=header), self.assertRaises(ValueError):
                frame_stats.read_samples(io.StringIO(header))
