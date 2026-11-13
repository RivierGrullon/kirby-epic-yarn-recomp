#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Summarize native --frame-log CSV files; does not measure display scanout."""
import argparse
import csv
import json
import math
from pathlib import Path
import sys


STAMPS = ("start_ns", "seal_start_ns", "seal_end_ns", "wait_end_ns", "finish_ns")
COUNTERS = ("vi_field", "surface_presents", "readbacks", "validation_bytes", "draws")
REQUIRED = {"sequence", "queued_pipelines", *STAMPS, *COUNTERS}


def read_samples(stream):
    reader = csv.DictReader(stream)
    fields = reader.fieldnames or []
    if len(fields) != len(set(fields)) or not REQUIRED.issubset(fields):
        raise ValueError("CSV header has missing or duplicate frame-log columns")
    samples = []
    for line, raw in enumerate(reader, 2):
        try:
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError("incomplete or extra fields")
            row = {key: int(raw[key]) for key in REQUIRED}
            if any(value < 0 for value in row.values()):
                raise ValueError("negative counter or timestamp")
            stamps = [row[key] for key in STAMPS]
            if stamps != sorted(stamps) or row["finish_ns"] <= row["start_ns"]:
                raise ValueError("presentation timestamps are out of order")
            if samples:
                previous = samples[-1]
                if row["sequence"] != previous["sequence"] + 1:
                    raise ValueError("submission sequence is not consecutive")
                if row["start_ns"] < previous["finish_ns"]:
                    raise ValueError("presentation intervals overlap")
                if any(row[key] < previous[key] for key in COUNTERS):
                    raise ValueError("cumulative counter decreased")
        except (ValueError, TypeError) as error:
            raise ValueError(f"Invalid frame record at line {line}: {error}") from error
        samples.append(row)
    return samples


def distribution(values):
    values = sorted(values)

    def percentile(fraction):
        index = (len(values) - 1) * fraction
        lo = int(index)
        hi = min(lo + 1, len(values) - 1)
        return values[lo] + (values[hi] - values[lo]) * (index - lo)

    return {"mean": sum(values) / len(values), "p50": percentile(.50),
            "p95": percentile(.95), "p99": percentile(.99), "max": values[-1]}


def summarize(samples, *, start=0, end=None, threshold_ms=25):
    if not math.isfinite(start) or start < 0:
        raise ValueError("Start must be a finite, nonnegative number of seconds")
    if end is not None and (not math.isfinite(end) or end <= start):
        raise ValueError("End must be finite and greater than start")
    if not math.isfinite(threshold_ms) or threshold_ms <= 0:
        raise ValueError("Long-interval threshold must be finite and positive")
    rows = [row for row in samples if row["start_ns"] >= start * 1e9 and
            (end is None or row["start_ns"] < end * 1e9)]
    if len(rows) < 2:
        raise ValueError("Selected window needs at least two complete presentations")

    intervals = []
    for previous, row in zip(rows, rows[1:]):
        intervals.append({
            "sequence": row["sequence"], "start_seconds": row["start_ns"] / 1e9,
            "interval_ms": (row["start_ns"] - previous["start_ns"]) / 1e6,
            "previous_selection_ms": (previous["seal_start_ns"] - previous["start_ns"]) / 1e6,
            "previous_seal_ms": (previous["seal_end_ns"] - previous["seal_start_ns"]) / 1e6,
            "previous_worker_wait_ms": (previous["wait_end_ns"] - previous["seal_end_ns"]) / 1e6,
            "previous_post_wait_ms": (previous["finish_ns"] - previous["wait_end_ns"]) / 1e6,
            "between_submissions_ms": (row["start_ns"] - previous["finish_ns"]) / 1e6,
            "vi_fields": row["vi_field"] - previous["vi_field"],
            "readbacks_since_previous_sample": row["readbacks"] - previous["readbacks"],
        })
    first, last = rows[0], rows[-1]
    seconds = (last["start_ns"] - first["start_ns"]) / 1e9
    stages = {}
    for name, before, after in (
        ("selection", "start_ns", "seal_start_ns"),
        ("seal", "seal_start_ns", "seal_end_ns"),
        ("worker_wait", "seal_end_ns", "wait_end_ns"),
        ("post_wait", "wait_end_ns", "finish_ns"),
    ):
        stages[name] = distribution([(row[after] - row[before]) / 1e6 for row in rows])
    stages["between_submissions"] = distribution([item["between_submissions_ms"] for item in intervals])
    return {
        "measurement": "Native VI submissions, not compositor scanout or game logic steps",
        "samples": len(rows), "first_sequence": first["sequence"], "last_sequence": last["sequence"],
        "start_seconds": first["start_ns"] / 1e9, "end_seconds": last["start_ns"] / 1e9,
        "interval_seconds": seconds, "submissions_per_second": (len(rows) - 1) / seconds,
        "interval_ms": distribution([item["interval_ms"] for item in intervals]),
        "stages_ms": stages,
        "counter_deltas": {key: last[key] - first[key] for key in COUNTERS},
        "queued_pipelines_peak": max(row["queued_pipelines"] for row in rows),
        "long_interval_threshold_ms": threshold_ms,
        "intervals_over_threshold": sum(item["interval_ms"] > threshold_ms for item in intervals),
        "longest_intervals": sorted(intervals, key=lambda item: item["interval_ms"], reverse=True)[:5],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--start", type=float, default=0, help="Inclusive SDL timestamp in seconds")
    parser.add_argument("--end", type=float, help="Exclusive SDL timestamp in seconds")
    parser.add_argument("--long-interval-ms", type=float, default=25, help="Diagnostic threshold; does not set an FPS target")
    args = parser.parse_args()
    try:
        with args.csv.open(newline="") as stream:
            rows = read_samples(stream)
        print(json.dumps(summarize(rows, start=args.start, end=args.end,
                                   threshold_ms=args.long_interval_ms), indent=2, allow_nan=False))
        return 0
    except (OSError, ValueError, csv.Error) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
