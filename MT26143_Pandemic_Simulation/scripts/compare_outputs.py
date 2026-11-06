
"""Compare daily CSV outputs byte-for-byte."""

from pathlib import Path
import sys

if len(sys.argv) < 3:
    raise SystemExit(
        "Usage: python compare_outputs.py "
        "baseline.csv other.csv [more.csv ...]"
    )

baseline = Path(sys.argv[1])
reference = baseline.read_bytes()
failed = False

for name in sys.argv[2:]:
    candidate = Path(name)

    if candidate.read_bytes() == reference:
        print(
            f"PASS: {candidate} matches {baseline}"
        )
    else:
        print(
            f"FAIL: {candidate} differs from {baseline}"
        )
        failed = True

if failed:
    raise SystemExit(1)

print("PASS: all CSV files are byte-for-byte identical")
