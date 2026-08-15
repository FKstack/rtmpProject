#!/usr/bin/env python3
"""Validate a scrubbed JSONL audio-loopback capture against release gates.

Each input line must contain sequence, sourceSubmittedMonotonicMs and
detectedMonotonicMs. Host/device names and stream URLs are intentionally not
accepted or copied into the report.
"""

import argparse
import json
import math
import pathlib
import statistics
import sys


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--minimum-samples", type=int, default=300)
    args = parser.parse_args()

    latencies = []
    sequences = set()
    with args.input.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            item = json.loads(line)
            sequence = int(item["sequence"])
            submitted = float(item["sourceSubmittedMonotonicMs"])
            detected = float(item["detectedMonotonicMs"])
            if sequence in sequences or detected < submitted:
                raise ValueError(f"invalid sample at line {line_number}")
            sequences.add(sequence)
            latencies.append(detected - submitted)

    p50 = percentile(latencies, 0.50)
    p95 = percentile(latencies, 0.95)
    maximum = max(latencies) if latencies else None
    passed = (
        len(latencies) >= args.minimum_samples
        and p50 is not None and p50 <= 100
        and p95 is not None and p95 <= 150
        and maximum is not None and maximum <= 250
    )
    report = {
        "schemaVersion": 1,
        "sampleCount": len(latencies),
        "p50Ms": round(p50, 3) if p50 is not None else None,
        "p95Ms": round(p95, 3) if p95 is not None else None,
        "maximumMs": round(maximum, 3) if maximum is not None else None,
        "meanMs": round(statistics.fmean(latencies), 3) if latencies else None,
        "gates": {"p50Ms": 100, "p95Ms": 150, "maximumMs": 250},
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
