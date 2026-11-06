
"""Part B: deterministic parallel correctness tests."""

from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

import csv
import sys

EXE = Path(sys.argv[1]).resolve()

THREAD_COUNTS = (1, 2, 3, 4, 7, 8, 16)
PARTITIONS = ("strips", "blocks", "interleaved")
MODES = ("spawn", "pool")


def launch(folder, name, args):
    output = folder / f"{name}.csv"

    command = [
        str(EXE),
        "--csv",
        str(output),
        *map(str, args)
    ]

    completed = run(
        command,
        capture_output=True,
        text=True,
        timeout=120
    )

    if completed.returncode:
        raise AssertionError(
            f"Command failed: {command}\n"
            f"{completed.stderr}"
        )

    return output.read_bytes()


with TemporaryDirectory() as temp:
    folder = Path(temp)
    checks = 0

    scenarios = (
        (
            "fixed",
            [
                "--grid", 19,
                "--days", 18,
                "--seed", 423,
                "--initial-infected", 16,
                "--vaccinated-fraction", 0.18,
                "--mortality", 0.17,
                "--fixed-days",
            ]
        ),
        (
            "early",
            [
                "--grid", 13,
                "--days", 40,
                "--seed", 71,
                "--initial-infected", 2,
                "--beta", 0,
                "--infectious-days", 3,
            ]
        ),
    )

    for label, base in scenarios:
        reference = launch(
            folder,
            f"{label}_sequential",
            [*base, "--mode", "sequential"]
        )

        path = folder / f"{label}_sequential.csv"

        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                total = sum(
                    int(row[s]) for s in "SIRVD"
                )

                N = int(
                    base[base.index("--grid") + 1]
                )

                assert total == N * N

        for t in THREAD_COUNTS:
            for partition in PARTITIONS:
                for mode in MODES:

                    c = (
                        t if mode == "spawn"
                        else 2 * t + 1
                    )

                    name = (
                        f"{label}_{mode}_{partition}"
                        f"_T{t}_C{c}"
                    )

                    result = launch(
                        folder,
                        name,
                        [
                            *base,
                            "--mode", mode,
                            "--threads", t,
                            "--chunks", c,
                            "--partition", partition,
                        ]
                    )

                    assert result == reference, (
                        f"Mismatch for {name}"
                    )

                    checks += 1

    # Test early exit with an empty epidemic.
    args = [
        "--grid", 5,
        "--initial-infected", 0,
        "--days", 10
    ]

    baseline = launch(
        folder, "empty_seq",
        [*args, "--mode", "sequential"]
    )

    parallel = launch(
        folder, "empty_pool",
        [
            *args,
            "--mode", "pool",
            "--threads", 7,
            "--chunks", 13,
            "--partition", "blocks"
        ]
    )

    assert parallel == baseline
    checks += 1

    # Test C > N and tiny grids.
    args = [
        "--grid", 3,
        "--initial-infected", 1,
        "--days", 4,
        "--seed", 9,
        "--fixed-days"
    ]

    baseline = launch(
        folder, "tiny_seq", args
    )

    for partition in PARTITIONS:
        parallel = launch(
            folder,
            f"tiny_{partition}",
            [
                *args,
                "--mode", "pool",
                "--threads", 4,
                "--chunks", 30,
                "--partition", partition
            ]
        )

        assert parallel == baseline
        checks += 1

    # Single worker and single chunk.
    for mode in MODES:
        result = launch(
            folder,
            f"one_{mode}",
            [
                *args,
                "--mode", mode,
                "--threads", 1,
                "--chunks", 1
            ]
        )

        assert result == baseline
        checks += 1

print(f"PASS: {checks} Part B equivalence checks")
