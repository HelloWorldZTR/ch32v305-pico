#!/usr/bin/env python3
"""Validate an 8-bit incrementing-counter logic-analyzer capture."""

from __future__ import annotations

import argparse
from pathlib import Path


def validate(samples: bytes) -> list[tuple[int, int, int]]:
    failures: list[tuple[int, int, int]] = []
    for index in range(1, len(samples)):
        expected = (samples[index - 1] + 1) & 0xFF
        if samples[index] != expected:
            failures.append((index, expected, samples[index]))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path, help="raw one-byte-per-sample file")
    parser.add_argument(
        "--sump-wire-order",
        action="store_true",
        help="reverse newest-first bytes received directly from a SUMP device",
    )
    parser.add_argument("--show", type=int, default=16, help="maximum failures to print")
    args = parser.parse_args()

    samples = args.capture.read_bytes()
    if args.sump_wire_order:
        samples = samples[::-1]
    if len(samples) < 2:
        parser.error("capture must contain at least two samples")

    failures = validate(samples)
    if failures:
        for index, expected, actual in failures[: args.show]:
            print(
                f"sample {index}: expected 0x{expected:02x}, got 0x{actual:02x}"
            )
        print(f"FAIL: {len(failures)} discontinuities in {len(samples)} samples")
        return 1

    print(f"PASS: {len(samples)} consecutive counter samples")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
