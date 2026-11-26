# Queen domination on even boards

The manuscript determines the ordinary domination numbers of the queen’s graph at $n=50,52,54$ as $26,27,28$, respectively. The lower bounds use a color-split frame reduction and DRAT-checked SAT refutations. The coordinate witnesses establish the upper bounds. The manuscript also supplies directly checkable independent dominating sets for five values already reported by [Neuhaus, §2.4](https://kups.ub.uni-koeln.de/2916/).

- Manuscript [PDF](https://github.com/CosmicCrusader23/queens/blob/main/paper/paper.pdf) and [LaTeX source](https://github.com/CosmicCrusader23/queens/blob/main/paper/paper.tex). The paper names the immutable code-and-data revision.
- [Mathematical reductions and branch tags](PROOFS.md).
- [Publication review and evidence](REVIEW.md).

## Files and results

| Board | Tested size | Canonical cases | Pruned cases | SAT leaves with recorded DRAT checks |
| ---: | ---: | ---: | ---: | ---: |
| 50 | 25 | 54,626 | 44,288 | 19,090 |
| 52 | 26 | 63,119 | 43,354 | 30,814 |
| 54 | 27 | 72,268 | 59,164 | 24,251 |

The `cert_n*_k*.jsonl` files in [results](results/) contain one complete audit log per board. Each leaf records its tag, DIMACS hash, solver verdict, and checker status. All 74,155 hashes match the pinned encoder, and all three logs ($n=50,52,54$) come from complete end-to-end DRAT-certified runs using the pinned encoder, solver, and checker (`--jobs 10 --config auto --certify`).

The Python auditor independently re-enumerates the branch tags and matches them to the records. The raw CNFs and DRAT proofs were temporary. The regeneration command below produces and checks them again.

## Build

Tested on arm64 macOS 26.5.2 with Apple Clang 17.0.0, C++20, and Python 3.14. The C++ driver uses macOS CommonCrypto and POSIX process APIs. The Python tools use the standard library and require Python 3.11 or later. The commands below start in an empty directory. After cloning, check out the full code-and-data SHA printed in the manuscript if reproducing that revision.

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

The dependency pins are [CaDiCaL 3.1.0](https://github.com/arminbiere/cadical/commit/e4d9bd93af444c6575a908a24da2c4130cb2a1c7) and [DRAT-trim](https://github.com/marijnheule/drat-trim/commit/2e3b2dc0ecf938addbd779d42877b6ed69d9a985). The default checker path is `build/drat-trim`. The driver uses binary DRAT, backward checking, `-w` to suppress warnings, and a `-t 1000000` checker timeout.

## Check the records and explicit witnesses

```sh
mkdir -p replay
python3 scripts/verify_certificate.py --witness-json results/even_upper_witnesses.json
for n in 50 52 54; do
  ./build/queen_prover --n "$n" --fingerprints > "replay/n$n.tsv" || exit 1
  python3 scripts/verify_certificate.py --n "$n" \
    --cert "results/cert_n${n}_k$((n / 2)).jsonl" \
    --fingerprints "replay/n$n.tsv" || exit 1
done
```

The auditor should print checked witnesses for 12 boards and confirm 19,090, 30,814, and 24,251 matching hashes. Run the Python programs with assertions enabled.

To search for another witness, run `./build/witness_sa --n 50 --mode even_plus1 --jobs 4 --timeout 30`. Add `--indep` to require a pairwise non-attacking set. The search is heuristic and can return a different placement or reach the time limit. The archived coordinates give the reproducible upper-bound check.

## Regenerate certificates

```sh
mkdir -p replay
for n in 50 52 54; do
  ./build/queen_prover --n "$n" --jobs 10 --config auto --certify \
    --out "replay/cert_n$n.jsonl" || test "$?" -eq 20 || exit 1
  python3 scripts/verify_certificate.py --n "$n" --cert "replay/cert_n$n.jsonl" || exit 1
done
```

An all-UNSAT run exits with code 20 and prints `UNSAT_CERTIFIED`. The expected verification counts are 19,090, 30,814, and 24,251. The default run deletes each temporary pair after checking. Add `--proof-dir replay/proofs-n50` (or a separate new directory for each board) to retain every pair. The `auto` solver configuration selects `unsat` for branch kinds 1, 2, 5, 7, 8, 11, and 12. It uses the default configuration for the other branches.

The number of candidate margin tuples grows as $O(n^4)$. The margin and Hall-capacity tests take up to $O(n)$ per case. Each formula starts from at most $n^2$ queen variables and adds occupancy and counter variables. SAT solve and check times depend on the formula and machine. Table 1 in the manuscript reports the recorded runtimes and proof sizes. Allow additional disk space for DIMACS files if retaining a full run.

## Validation and timing

```sh
mkdir -p replay
python3 scripts/check_small_boards.py
python3 bench/verification_overhead.py > replay/benchmark-overhead-n26.json
python3 bench/algebraic_pruning.py --jobs 10 --repetitions 3 > replay/benchmark-pruning-n26.json
```

The small-board check compares direct enumeration at $n=4,6,8$ with the SAT pipeline with and without within-case lex constraints. The overhead benchmark uses the same 99 leaves of $n=26$, the same solver settings, and one worker on the same machine. It measures with and without proof generation and checking. The pruning benchmark runs the complete certified $n=26$ computation, toggling only the algebraic case-pruning pass. It holds the solver, checker, machine, and worker count fixed. Both experiments alternate which mode goes first across three repeats. [Proof overhead](results/benchmark-overhead-n26.json) · [Algebraic pruning](results/benchmark-pruning-n26.json).

The summary fields `solve_elapsed_sum_s` and `check_elapsed_sum_s` sum worker elapsed times (renamed from `*_cpu_s` in earlier revisions). The solver timer covers `solve()` and proof writes during that call. The checker timer includes starting and waiting for the checker. Worker wall time begins after case generation and ordering. Published Davies timings have different definitions and execution environments. [Davies, Table 2](https://hexagonmath.org/2611.00830v1).

## Provenance

Gemini 4 Neon, through Google Antigravity, performed the original mathematical research and computations and produced the original code and manuscript. OpenAI Codex assisted with the adversarial review, reproduction checks, and revision. The frame method builds on [*Hint and Conquer*](https://github.com/TahaRostami/Gamma/blob/hint-and-conquer/doc.pdf) and [Davies](https://hexagonmath.org/2611.00830v1).
