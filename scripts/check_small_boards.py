#!/usr/bin/env python3
"""Compare the SAT pipeline with exhaustive direct enumeration on three boards."""

import argparse
from functools import reduce
from itertools import combinations
import json
from operator import or_
from pathlib import Path
import subprocess


def count_dominating_sets(n):
    squares = [(x, y) for x in range(n) for y in range(n)]
    masks = [
        sum(1 << j for j, (a, b) in enumerate(squares)
            if a == x or b == y or abs(a - x) == abs(b - y))
        for x, y in squares
    ]
    full = (1 << (n * n)) - 1
    return sum(reduce(or_, subset, 0) == full for subset in combinations(masks, n // 2))


def check_prover(n, count, prover, checker):
    command = [
        str(prover.resolve()), "--n", str(n), "--jobs", "1", "--certify",
        "--drat-trim", str(checker.resolve()),
    ]
    for disable_sbp in (False, True):
        result = subprocess.run(
            command + (["--no-sbp"] if disable_sbp else []),
            stdout=subprocess.PIPE, text=True, check=False,
        )
        if result.returncode != (10 if count else 20):
            raise SystemExit(f"Q_{n}: unexpected solver exit {result.returncode}")
        summary = json.loads(result.stdout.splitlines()[-1])["summary"]
        if summary["errors"]:
            raise SystemExit(f"Q_{n}: {summary['errors']} errors reported")
        if count:
            witness = [tuple(point) for point in summary["witness"]]
            if len(witness) != n // 2 or len(set(witness)) != len(witness):
                raise SystemExit(f"Q_{n}: wrong witness size")
            if any(not (0 <= a < n and 0 <= b < n) for a, b in witness):
                raise SystemExit(f"Q_{n}: out-of-range witness square")
            if any(
                not any(a == x or b == y or abs(a - x) == abs(b - y) for a, b in witness)
                for x in range(n) for y in range(n)
            ):
                raise SystemExit(f"Q_{n}: a square is undominated")
        elif summary["verified"] != summary["subproblems"]:
            raise SystemExit(f"Q_{n}: incomplete DRAT checks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prover", type=Path, default=Path("build/queen_prover"))
    parser.add_argument("--checker", type=Path, default=Path("build/drat-trim"))
    args = parser.parse_args()
    for n in (4, 6, 8):
        count = count_dominating_sets(n)
        check_prover(n, count, args.prover, args.checker)
        print(f"Q_{n}: exhaustive count {count}, SAT pipeline agrees with and without lex constraints")


if __name__ == "__main__":
    main()
