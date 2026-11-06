
"""Automated correctness tests for Part A."""

import csv
import pathlib
import subprocess
import sys
import tempfile

exe = pathlib.Path(sys.argv[1]).resolve()
checks = 0

with tempfile.TemporaryDirectory() as folder:
    base = pathlib.Path(folder)

    def run(name, *opts, success=True):
        csv_path = base / (name + ".csv")

        proc = subprocess.run(
            [str(exe), "--csv", str(csv_path),
             *map(str, opts)],
            capture_output=True,
            text=True,
        )

        if success and proc.returncode != 0:
            raise AssertionError(
                f"{name}: {proc.stderr}"
            )

        if not success:
            assert proc.returncode != 0, name
            return proc

        with csv_path.open(newline="") as handle:
            rows = list(csv.DictReader(handle))

        return rows, csv_path.read_bytes()

    # Test 1-3: Determinism and consistency
    shared = [
        "--grid", 17,
        "--days", 22,
        "--seed", 1234,
        "--initial-infected", 12,
        "--vaccinated-fraction", 0.15,
        "--fixed-days",
    ]

    a, raw_a = run("repeatA", *shared)
    b, raw_b = run("repeatB", *shared)

    assert raw_a == raw_b
    checks += 1

    assert len(a) == 23
    checks += 1

    for row in a:
        assert sum(int(row[x]) for x in "SIRVD") == 289
        assert len(row["checksum"]) == 16

    checks += 1

    # Test 4: Reserved thread options
    c, raw_c = run(
        "paramsReserved", *shared,
        "--threads", 7,
        "--chunks", 23,
        "--partition", "interleaved",
    )

    assert raw_a == raw_c
    checks += 1

    # Test 5: Recovery
    rows, _ = run(
        "oneRecovers",
        "--grid", 1,
        "--initial-infected", 1,
        "--infectious-days", 2,
        "--mortality", 0,
        "--days", 8,
    )

    assert [
        (int(r["I"]), int(r["R"]))
        for r in rows
    ] == [(1, 0), (1, 0), (0, 1)]

    checks += 1

    # Test 6: Death
    rows, _ = run(
        "oneDies",
        "--grid", 1,
        "--initial-infected", 1,
        "--infectious-days", 1,
        "--mortality", 1,
        "--days", 8,
    )

    assert [
        (int(r["I"]), int(r["D"]))
        for r in rows
    ] == [(1, 0), (0, 1)]

    checks += 1

    # Test 7: Vaccination
    rows, _ = run(
        "allVaccinated",
        "--grid", 3,
        "--initial-infected", 0,
        "--vaccinated-fraction", 1,
        "--days", 10,
    )

    assert len(rows) == 1
    assert int(rows[0]["V"]) == 9
    checks += 1

    # Test 8: No transmission
    rows, _ = run(
        "betaZero",
        "--grid", 8,
        "--initial-infected", 1,
        "--beta", 0,
        "--infectious-days", 2,
        "--days", 8,
    )

    assert max(int(r["I"]) for r in rows) == 1
    checks += 1

    # Test 9: Certain transmission
    rows, _ = run(
        "betaOne",
        "--grid", 2,
        "--initial-infected", 1,
        "--beta", 1,
        "--infectious-days", 7,
        "--lockdown-on", 1,
        "--days", 1,
    )

    assert int(rows[1]["I"]) == 4
    checks += 1

    # Test 10: Lockdown and masks
    rows, _ = run(
        "lockdown",
        "--grid", 2,
        "--initial-infected", 1,
        "--beta", 0.8,
        "--mask-factor", 0.5,
        "--lockdown-factor", 0.25,
        "--lockdown-on", 0.2,
        "--lockdown-off", 0.1,
        "--days", 1,
    )

    assert abs(
        float(rows[1]["beta_eff"]) - 0.1
    ) < 1e-12

    checks += 1

    # Test 11: PPM snapshot
    snap = base / "snapshots"

    run(
        "snap",
        "--grid", 3,
        "--initial-infected", 0,
        "--days", 0,
        "--snapshot-every", 1,
        "--snapshot-dir", snap,
    )

    ppm = (snap / "day_0.ppm").read_bytes()
    header = b"P6\n3 3\n255\n"

    assert ppm.startswith(header)
    assert len(ppm) == len(header) + 27

    checks += 1

    # Test 12: Invalid mortality
    run(
        "badMortality",
        "--mortality", 1.5,
        success=False,
    )
    checks += 1

    # Test 13: Invalid initial infection
    run(
        "badInfected",
        "--grid", 1,
        "--initial-infected", 2,
        success=False,
    )
    checks += 1

    # Test 14: Async not supported in Part A
    run(
        "badSnapshotAsync",
        "--snapshot-every", 1,
        "--snapshot-mode", "async",
        success=False,
    )
    checks += 1

print(f"PASS: {checks} Part A checks")
