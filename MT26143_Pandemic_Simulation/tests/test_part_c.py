
"""Part C correctness tests."""

import csv
import subprocess
import sys
import tempfile
from pathlib import Path

SIM = Path(sys.argv[1]).resolve()
SWEEP = Path(sys.argv[2]).resolve()

checks = 0


def command(executable, *args, ok=True, timeout=90):
    proc = subprocess.run(
        [str(executable), *map(str, args)],
        capture_output=True,
        text=True,
        timeout=timeout,
    )

    if ok and proc.returncode:
        raise AssertionError(
            f"{executable.name}: {proc.stderr}"
        )

    if not ok:
        assert proc.returncode != 0

    return proc


with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)

    # C1: Blocking and async snapshots.
    base = [
        "--grid", 18,
        "--days", 6,
        "--seed", 41,
        "--initial-infected", 12,
        "--fixed-days",
        "--mode", "pool",
        "--threads", 3,
        "--chunks", 7,
        "--partition", "blocks",
        "--snapshot-every", 2,
        "--io-latency", 10,
    ]

    csvs = {}
    images = {}

    for mode, pending in [
        ("blocking", 2),
        ("async", 1),
        ("async", 3),
        ("off", 2),
    ]:
        tag = f"{mode}_{pending}"

        path = root / f"{tag}.csv"
        dest = root / tag

        command(
            SIM,
            *base,
            "--snapshot-mode", mode,
            "--max-pending-writes", pending,
            "--snapshot-dir", dest,
            "--csv", path,
        )

        csvs[tag] = path.read_bytes()

        images[tag] = {
            p.name: p.read_bytes()
            for p in dest.glob("*.ppm")
        }

    assert len(set(csvs.values())) == 1
    checks += 1

    assert sorted(images["blocking_2"]) == [
        f"day_{day}.ppm"
        for day in (0, 2, 4, 6)
    ]
    checks += 1

    assert (
        images["blocking_2"]
        == images["async_1"]
        == images["async_3"]
    )
    checks += 1

    assert not images["off_2"]
    checks += 1

    # Test disabling daily CSV output.
    no_csv = root / "nothing.csv"

    proc = command(
        SIM,
        "--grid", 12,
        "--days", 3,
        "--initial-infected", 2,
        "--no-csv",
        "--csv", no_csv,
    )

    assert not no_csv.exists()
    assert "Simulation loop seconds:" in proc.stdout
    checks += 1

    # C2: Cartesian parameter sweep.
    output = root / "sweep.csv"

    command(
        SWEEP,
        "--grid", 18,
        "--days", 9,
        "--betas", "0,0.08",
        "--vaccinations", "0,0.2",
        "--seeds", "41,42",
        "--concurrent-runs", 3,
        "--time-limit", 0,
        "--output", output,
    )

    with output.open(newline="") as f:
        rows = list(csv.DictReader(f))

    with (root / "sweep_missing.csv").open(
        newline=""
    ) as f:
        missing = list(csv.DictReader(f))

    assert len(rows) == 8
    assert not missing
    checks += 1

    expected = {
        (b, v, s)
        for b in (0.0, 0.08)
        for v in (0.0, 0.2)
        for s in (41, 42)
    }

    actual = {
        (
            float(r["beta"]),
            float(r["vaccinated_fraction"]),
            int(r["seed"]),
        )
        for r in rows
    }

    assert actual == expected
    checks += 1

    assert all(
        0 <= float(r["peak_infected_fraction"]) <= 1
        and 0 <= int(r["peak_day"]) <= int(r["days"])
        and int(r["total_infected"]) >= 10
        and int(r["total_deaths"])
            <= int(r["total_infected"])
        for r in rows
    )
    checks += 1

    # Validate one sweep result against
    # the ordinary simulation CSV.
    check_csv = root / "check.csv"

    command(
        SIM,
        "--grid", 18,
        "--days", 9,
        "--beta", 0,
        "--seed", 41,
        "--vaccinated-fraction", 0,
        "--csv", check_csv,
    )

    with check_csv.open(newline="") as f:
        baseline = list(csv.DictReader(f))

    reference = next(
        r for r in rows
        if float(r["beta"]) == 0
        and float(r["vaccinated_fraction"]) == 0
        and int(r["seed"]) == 41
    )

    peak = max(
        enumerate(baseline),
        key=lambda pair: int(pair[1]["I"])
    )

    assert int(reference["peak_day"]) == peak[0]

    assert abs(
        float(reference["peak_infected_fraction"])
        - int(peak[1]["I"]) / (18 * 18)
    ) < 1e-14

    assert int(reference["total_infected"]) == 10

    assert (
        int(reference["total_deaths"])
        == int(baseline[-1]["D"])
    )

    assert int(reference["days"]) == len(baseline) - 1

    checks += 1

    # Deadline: unfinished work should not
    # be interrupted, but unstarted runs
    # should be recorded as missing.
    cutoff = root / "cutoff.csv"

    command(
        SWEEP,
        "--grid", 700,
        "--days", 30,
        "--fixed-days",
        "--betas", "0.03,0.08",
        "--vaccinations", "0,0.1",
        "--seeds", "1,2,3,4",
        "--concurrent-runs", 2,
        "--time-limit", 0.001,
        "--output", cutoff,
        timeout=120,
    )

    with cutoff.open(newline="") as f:
        done = list(csv.DictReader(f))

    with (root / "cutoff_missing.csv").open(
        newline=""
    ) as f:
        omitted = list(csv.DictReader(f))

    assert len(done) + len(omitted) == 16
    assert len(omitted) > 0

    assert all(
        r["reason"] == "not_started_deadline"
        for r in omitted
    )

    checks += 1

    # Nested parallel simulations.
    nested = root / "nested.csv"

    command(
        SWEEP,
        "--grid", 16,
        "--days", 7,
        "--fixed-days",
        "--betas", "0.08",
        "--vaccinations", "0",
        "--seeds", "5,6",
        "--concurrent-runs", 2,
        "--mode", "pool",
        "--threads", 2,
        "--chunks", 5,
        "--partition", "interleaved",
        "--output", nested,
    )

    with nested.open(newline="") as f:
        assert len(list(csv.DictReader(f))) == 2

    checks += 1

    # Reject invalid sweep parameters.
    command(
        SWEEP,
        "--betas", "2.4",
        ok=False,
    )

    checks += 1

print(f"PASS: {checks} Part C checks")
