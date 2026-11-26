#!/usr/bin/env python3
"""Time the full certified Q_26 computation with and without algebraic pruning."""

import argparse
import json
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time


def run(command, prune):
    extra = [] if prune else ["--no-alg-prune"]
    started = time.perf_counter()
    process = subprocess.run(command + extra, stdout=subprocess.PIPE, text=True, check=False)
    elapsed = time.perf_counter() - started
    if process.returncode != 20:
        raise SystemExit(f"certified UNSAT expected, got exit {process.returncode}")
    summary = json.loads(process.stdout.splitlines()[-1])["summary"]
    if not (
        summary["verdict"] == "UNSAT_CERTIFIED"
        and summary["alg_prune"] is prune
        and summary["certify"]
        and summary["sbp"]
        and summary["errors"] == summary["sat"] == 0
        and summary["unsat"] == summary["verified"] == summary["subproblems"] > 0
    ):
        raise SystemExit(f"incomplete or differently configured run: {summary}")
    return {
        "elapsed_s": round(elapsed, 4),
        **{key: summary[key] for key in (
            "canonical_cases", "subproblems", "verified", "wall_s",
            "solve_elapsed_sum_s", "check_elapsed_sum_s", "proof_mb",
        )},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prover", type=Path, default=Path("build/queen_prover"))
    parser.add_argument("--checker", type=Path, default=Path("build/drat-trim"))
    parser.add_argument("--jobs", type=int, default=10)
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    if args.jobs < 1 or args.repetitions < 1:
        parser.error("--jobs and --repetitions must be positive")

    measurements = {"pruning_on": [], "pruning_off": []}
    with tempfile.TemporaryDirectory() as temp:
        command = [
            str(args.prover.resolve()), "--n", "26", "--jobs", str(args.jobs),
            "--config", "auto", "--certify", "--drat-trim", str(args.checker.resolve()),
            "--tmp", temp,
        ]
        for repeat in range(args.repetitions):
            # Alternate the order to reduce systematic warm-up effects.
            for prune in ((True, False) if repeat % 2 == 0 else (False, True)):
                key = "pruning_on" if prune else "pruning_off"
                measurements[key].append(run(command, prune))

    medians = {
        mode: statistics.median(row["elapsed_s"] for row in rows)
        for mode, rows in measurements.items()
    }
    print(json.dumps({
        "n": 26,
        "platform": {"system": platform.system(), "release": platform.release(),
                     "arch": platform.machine()},
        "workers": args.jobs,
        "solver_configuration": "auto",
        "checker_options": ["-w", "-t", "1000000"],
        "wall_clock": "Python perf_counter around the complete prover command",
        "measurements": measurements,
        "median_elapsed_s": medians,
        "off_to_on_median_ratio": medians["pruning_off"] / medians["pruning_on"],
    }, indent=2))


if __name__ == "__main__":
    main()
