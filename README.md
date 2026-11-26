# Queen domination on even boards

The manuscript determines the ordinary domination numbers of the queen’s graph for all eight even boards $n=50,52,54,56,58,60,62,64$ as $\gamma(Q_n) = n/2 + 1$, namely $26,27,28,29,30,31,32,33$. Each lower bound uses a color-split frame reduction and binary DRAT proofs checked by unmodified `drat-trim`. Explicit coordinate witnesses attain every upper bound; the $Q_{60}$ witness is pairwise non-attacking, additionally certifying $i(Q_{60}) = 31$. The repository also supplies directly checkable independent dominating sets for five earlier values reported by [Neuhaus, §2.4](https://kups.ub.uni-koeln.de/2916/) ($n=32,34,38,40,44$).

- Manuscript [PDF](https://github.com/CosmicCrusader23/queens/blob/main/paper/paper.pdf) and [LaTeX source](https://github.com/CosmicCrusader23/queens/blob/main/paper/paper.tex). The paper cites the immutable computational snapshot commit.
- [Mathematical reductions and branch tags](PROOFS.md).
- [Adversarial review and audit log](REVIEW.md).
- Previous $Q_{50}\text{--}Q_{54}$ pinned reproducibility snapshot: commit [`7872bd98a9d1d72d463c6b967717bd7ab093b7d8`](https://github.com/CosmicCrusader23/queens/tree/7872bd98a9d1d72d463c6b967717bd7ab093b7d8) (manuscript revision [`47c85f605a358af55ec60313ffa6ae59b06ff236`](https://github.com/CosmicCrusader23/queens/tree/47c85f605a358af55ec60313ffa6ae59b06ff236)). All original $Q_{50}\text{--}Q_{54}$ certification logs are preserved unchanged.

## Certified results and resource usage

| Board | Tested $k$ | Canonical cases | Pruned cases (%) | SAT leaves | Solve sum (s) | Check sum (s) | Wall (s) | DRAT (GB) | Peak RSS (MB) | Peak temp (MB) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 50 | 25 | 54,626 | 44,288 (81.1%) | 19,090 | 1,799.29 | 2,703.82 | 492.68 | 2.69 | — | — |
| 52 | 26 | 63,119 | 43,354 (68.7%) | 30,814 | 5,539.94 | 5,434.66 | 1,170.76 | 6.21 | — | — |
| 54 | 27 | 72,268 | 59,164 (81.9%) | 24,251 | 3,512.24 | 4,388.23 | 856.67 | 4.35 | — | — |
| 56 | 28 | 82,695 | 57,564 (69.6%) | 39,144 | 7,947.76 | 6,673.64 | 1,551.10 | 9.53 | 929.2 | 208.0 |
| 58 | 29 | 93,870 | 77,479 (82.5%) | 30,409 | 5,272.34 | 5,162.29 | 1,434.49 | 7.23 | 1,769.5 | 683.0 |
| 60 | 30 | 106,504 | 74,991 (70.4%) | 48,856 | 13,191.39 | 9,754.24 | 2,418.80 | 14.67 | 1,335.3 | 519.1 |
| 62 | 31 | 119,984 | 99,772 (83.2%) | 37,571 | 8,719.97 | 8,275.18 | 5,020.18 | 13.09 | 4,896.0 | 3,066.6 |
| 64 | 32 | 135,116 | 96,115 (71.1%) | 60,407 | 17,674.78 | 13,420.81 | 3,270.29 | 22.58 | 3,407.8 | 1,953.9 |
| **Total** | | **728,182** | **552,727 (75.9%)** | **290,542** | **63,657.71** | **55,812.87** | **16,214.97** | **80.34** | **4,896.0** | **3,066.6** |

The `cert_n*_k*.jsonl` files in [results](results/) contain one complete audit log per board. Each leaf records its tag, DIMACS SHA-256 hash, solver verdict, proof byte size, and checker status. All 290,542 recorded hashes match fresh regeneration with the pinned encoder. The `diagnostics_n*_k*.json` files record wall clocks, peak parent and child RSS, peak temporary disk footprint, minimum free disk space, swap usage, and macOS VM pressure levels for the five new runs ($n=56\text{--}64$). Temporary CNF and DRAT files were deleted immediately after successful DRAT verification; any failure or unexpected SAT preserves its `.cnf`/`.drat` artifacts for inspection.

## Build

Tested on a ten-core Apple M4 (arm64, 16 GB RAM) running macOS 26.5.2 (Darwin 25.5.0) with Apple Clang 17.0.0, C++20, and Python 3.14.0. The C++ driver uses macOS CommonCrypto and POSIX process APIs. The Python scripts use only the standard library (Python 3.11+).

```sh
git clone https://github.com/CosmicCrusader23/queens.git
cd queens
mkdir -p tools
git clone https://github.com/arminbiere/cadical.git tools/cadical
git -C tools/cadical checkout --detach e4d9bd93af444c6575a908a24da2c4130cb2a1c7
(cd tools/cadical && ./configure && make -j4)
git clone https://github.com/marijnheule/drat-trim.git tools/drat-trim-src
git -C tools/drat-trim-src checkout --detach 2e3b2dc0ecf938addbd779d42877b6ed69d9a985
make -j4
```

Pinned upstream commits: [CaDiCaL 3.1.0](https://github.com/arminbiere/cadical/commit/e4d9bd93af444c6575a908a24da2c4130cb2a1c7) (`e4d9bd93af444c6575a908a24da2c4130cb2a1c7`) and [DRAT-trim](https://github.com/marijnheule/drat-trim/commit/2e3b2dc0ecf938addbd779d42877b6ed69d9a985) (`2e3b2dc0ecf938addbd779d42877b6ed69d9a985`). The checker runs in backward mode with binary DRAT input and options `-w -t 1000000`.

## Audit recorded certificates and explicit witnesses

```sh
mkdir -p replay
python3 scripts/verify_certificate.py --witness-json results/even_upper_witnesses.json
for n in 50 52 54 56 58 60 62 64; do
  ./build/queen_prover --n "$n" --jobs 10 --fingerprints > "replay/n$n.tsv" || exit 1
  python3 scripts/verify_certificate.py --n "$n" \
    --cert "results/cert_n${n}_k$((n / 2)).jsonl" \
    --fingerprints "replay/n$n.tsv" || exit 1
done
```

This checks all 17 stored witnesses ($n=32,34,\dots,64$, including independent sets for $n=32,34,38,40,44,60$) and confirms all 290,542 regenerated DIMACS SHA-256 digests against the JSONL logs. Run Python with assertions enabled (do not pass `-O`).

## Regenerate certificates

For resource-monitored, resumable certification (suspending cleanly if free disk drops below 8 GB or macOS VM pressure hits critical, and never using `--proof-dir` on full runs):

```sh
mkdir -p replay
for n in 50 52 54 56 58 60 62 64; do
  python3 -u scripts/run_certified_board.py --n "$n" --jobs 10 \
    --min-free-gb 8.0 --max-pressure 4 \
    --out "replay/cert_n${n}_k$((n / 2)).jsonl" \
    --diag-out "replay/diagnostics_n${n}_k$((n / 2)).json" || exit $?
done
```

Direct invocation without the Python monitor is unchanged:

```sh
./build/queen_prover --n 56 --jobs 10 --config auto --certify \
  --drat-trim ./build/drat-trim --out replay/cert_n56_k28.jsonl
```

An all-UNSAT certified run exits with code 20 and prints `UNSAT_CERTIFIED`. The `auto` configuration selects CaDiCaL's `unsat` preset for branch kinds 1, 2, 5, 7, 8, 11, and 12, and the default preset otherwise. Expensive `(0,1,0,1)` leaves (branch 8 and `(0,1,0,1)` branch 3) are scheduled at top priority so they overlap with the bulk work queue; this affects only execution order and leaves all generated formulas and hashes bit-for-bit identical.

To search for alternative upper-bound witnesses, run `./build/witness_sa --n 56 --mode even_plus1 --jobs 10 --timeout 60` (add `--indep` to require independence).

## Validation and controlled benchmarks

```sh
mkdir -p replay
python3 scripts/check_small_boards.py
python3 bench/verification_overhead.py > replay/benchmark-overhead-n26.json
python3 bench/algebraic_pruning.py --jobs 10 --repetitions 3 > replay/benchmark-pruning-n26.json
```

`check_small_boards.py` checks exact agreement with exhaustive bit-mask enumeration on $Q_4, Q_6, Q_8$ both with and without within-case lex constraints. Controlled single-machine benchmark logs on $n=26$ are archived in [results/benchmark-overhead-n26.json](results/benchmark-overhead-n26.json) and [results/benchmark-pruning-n26.json](results/benchmark-pruning-n26.json).

## Provenance

Gemini 4 Neon, through Google Antigravity, carried out the original mathematical formulation, C++ and Python implementation, computations ($n=50\text{--}64$), verification scripts, and manuscript drafting. OpenAI Codex assisted with an earlier adversarial review, reproduction checks, and manuscript revisions for $n=50\text{--}54$. The human author initiated and coordinated the project and reviewed the automated computation logs and verification outputs. Automated verification and AI adversarial review do not constitute independent human mathematical verification.
