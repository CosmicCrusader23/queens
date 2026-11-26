#!/usr/bin/env python3
"""Compare the same leaves with and without DRAT generation and checking."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prover', type=Path, default=Path('build/queen_prover'))
    parser.add_argument('--checker', type=Path, default=Path('build/drat-trim'))
    parser.add_argument('--repetitions', type=int, default=3)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error('--repetitions must be positive')

    board = 26
    leaves = subprocess.check_output(
        [str(args.prover.resolve()), '--n', str(board), '--fingerprints', '--jobs', '1'],
        text=True,
    ).splitlines()
    by_kind = {}
    for line in leaves:
        tag, _ = line.split('\t')
        kind = int(tag.split('_b', 1)[1].split('_', 1)[0])
        by_kind.setdefault(kind, []).append(tag)
    tags = sorted(
        tag
        for group in by_kind.values()
        for tag in sorted(group, key=lambda s: hashlib.sha256(s.encode()).digest())[:12]
    )
    measurements = {'plain': [], 'certified': []}
    with tempfile.TemporaryDirectory() as temp:
        tagfile = Path(temp) / 'tags.txt'
        tagfile.write_text('\n'.join(tags) + '\n')
        command = [
            str(args.prover.resolve()), '--n', str(board), '--jobs', '1',
            '--config', 'auto', '--tags', str(tagfile), '--tmp', temp,
        ]
        for repetition in range(args.repetitions):
            order = ('plain', 'certified') if repetition % 2 == 0 else ('certified', 'plain')
            for mode in order:
                extra = ['--certify', '--drat-trim', str(args.checker.resolve())] if mode == 'certified' else []
                result = subprocess.run(command + extra, text=True, stdout=subprocess.PIPE, check=False)
                if result.returncode != 20:
                    raise SystemExit(f'{mode} failed with code {result.returncode}')
                data = json.loads(result.stdout.splitlines()[-1])['summary']
                assert data['errors'] == 0 and data['unsat'] == len(tags)
                assert data['verified'] == (len(tags) if extra else 0)
                keys = ('wall_s', 'solve_elapsed_sum_s', 'check_elapsed_sum_s', 'verified')
                measurements[mode].append({key: data[key] for key in keys})

    print(json.dumps({
        'n': board,
        'platform': {'system': platform.system(), 'arch': platform.machine()},
        'solver_configuration': 'auto',
        'workers': 1,
        'leaves': len(tags),
        'branch_kinds': sorted(by_kind),
        'selection': '12 lowest SHA-256 tag hashes in each branch kind, or all tags for smaller kinds',
        'measurements_s': measurements,
        'median_wall_s': {
            mode: statistics.median(row['wall_s'] for row in runs)
            for mode, runs in measurements.items()
        },
    }, indent=2))


if __name__ == '__main__':
    main()
