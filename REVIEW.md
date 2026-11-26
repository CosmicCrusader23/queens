# Adversarial review and audit record

## Corrections and evidence ($Q_{50}\text{--}Q_{54}$ baseline and $Q_{56}\text{--}Q_{64}$ extension)

| Finding / Check | Resolution and evidence |
| --- | --- |
| Uncontrolled cross-machine speedup ratios in early draft. | Removed uncontrolled comparison tables and projections; kept controlled single-machine $n=26$ benchmarks (`results/benchmark-overhead-n26.json`, `results/benchmark-pruning-n26.json`) and separated measured data from scaling discussion. |
| Disjointness wording and parity-counter caps. | Section 6 proves the covering theorem (`thm:coverage`), the parity-counter upper bounds (`lem:caps`, including $k=2,3$), and auxiliary variable extensions. |
| Border-waste deduplication for odd-sided square with even $k$ and signed offset for single diagonal deficit. | Sections 5.4–5.5 derive all identities rigorously from Theorem $F_q, V_q, H_q$ and verify that $\waste_F^{(q)} = \waste_V^{(q)} + \waste_H^{(q)}$ when the color-$q$ box diagonal is occupied. |
| Scheduling-only prioritization of `(0,1,0,1)` leaves (`b8`, `b3`, `b12`) and failure-artifact preservation added for $Q_{56}\text{--}Q_{64}$. | Verified zero change to generated formulas: `check_small_boards.py` passes on $Q_4, Q_6, Q_8$ with and without SBP, and all 74,155 regenerated DIMACS SHA-256 hashes on archived $Q_{50}\text{--}Q_{54}$ logs match bit-for-bit. |
| Resumability and resource monitoring for $Q_{56}\text{--}Q_{64}$. | Executed sequentially via `scripts/run_certified_board.py` with 0.5 s polling of free disk, RSS, swap, and macOS VM pressure; no run triggered suspension (`min_free_disk_gb >= 21.06 GB`, `max_vm_pressure_level <= 2`, zero resumed segments needed). |
| Independent domination priority and new $i(Q_{60}) = 31$ result. | Neuhaus (§2.4, pp. 17–18) reported $i(Q_n)$ for $n=32,34,38,40,44$ (with Östergård & Weakley 2001 giving independent upper bounds for $34,38,40,44$). For $n=60$, our certified lower bound $\gamma(Q_{60}) = 31$ combined with a newly verified independent 31-queen placement establishes $i(Q_{60}) = 31$ (OEIS `A075324` DATA currently stops at $n=31$). |
| Literature and novelty verification for $Q_{56}\text{--}Q_{64}$ (checked Nov 27, 2026). | Live checks of OEIS `A075458` (DATA ends at $n=46$, plus Đukić's Nov 22, 2026 comment for $n=48$), OEIS `A075324`, Hexagon Math (`2611.00830v1`), Davies's GitHub, Neuhaus (2009), and Weakley (2022) confirm that $\gamma(Q_n)$ for $n=56,58,60,62,64$ had only the two-value range $\{n/2, n/2+1\}$ (Östergård & Weakley 2001) and monochromatic non-existence (Weakley 2022). |

## Mathematical completeness audit for $n = 50, 52, 54, 56, 58, 60, 62, 64$

- **Uniform applicability ($n = 2k \ge 4$):** All frame definitions, $D_4$ parity actions, color-split identities (Theorem 3.1), algebraic exclusions (Lemmas 4.1–4.4), branch decompositions (Section 5), parity-counter bounds (Lemma 6.1), and the coverage theorem (Theorem 6.2) hold verbatim for every even $n = 2k \ge 4$ without modification for $n \in \{56, 58, 60, 62, 64\}$.
- **Integer and solver bounds:** For $n = 64$ ($k = 32$), the quadratic moment right-hand side $4 \sum_{i=0}^{31} i^2 = 41\,664$ fits comfortably in 64-bit `long long`, diagonal indices lie in $[-63, 63]$ and $[0, 126]$, maximum formula dimensions stay under $2.6 \times 10^4$ variables and $1.1 \times 10^5$ clauses, and the largest `drat-trim` check time (`2,544.16 s` on $Q_{62}$) stays three orders of magnitude below the `-t 1000000` limit.
- **Small boards:** `scripts/check_small_boards.py` verifies exact agreement with exhaustive bit-mask enumeration ($12$, $4$, and $0$ dominating $n/2$-sets for $n=4,6,8$) both with and without within-case lex constraints.
- **Upper bounds:** All 17 stored placements ($Q_{32}, Q_{34}, \dots, Q_{64}$) pass direct full-board coverage and independence checks in `scripts/verify_certificate.py`, and all coordinate lists printed in the manuscript match `results/even_upper_witnesses.json` verbatim.

## Complete computational verification coverage

| Board | Tested $k$ | Canonical cases | Pruned | Verified leaves | Matching SHA-256 hashes | DRAT (GB) | Peak RSS (MB) | Peak temp (MB) | Log provenance |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 50 | 25 | 54,626 | 44,288 | 19,090 | 19,090 | 2.69 | — | — | Pinned encoder (`7872bd98`), bit-identical hashes |
| 52 | 26 | 63,119 | 43,354 | 30,814 | 30,814 | 6.21 | — | — | Pinned encoder (`7872bd98`), bit-identical hashes |
| 54 | 27 | 72,268 | 59,164 | 24,251 | 24,251 | 4.35 | — | — | Pinned encoder (`7872bd98`), bit-identical hashes |
| 56 | 28 | 82,695 | 57,564 | 39,144 | 39,144 | 9.53 | 929.2 | 208.0 | Complete certified run (`run_certified_board.py`) |
| 58 | 29 | 93,870 | 77,479 | 30,409 | 30,409 | 7.23 | 1,769.5 | 683.0 | Complete certified run (`run_certified_board.py`) |
| 60 | 30 | 106,504 | 74,991 | 48,856 | 48,856 | 14.67 | 1,335.3 | 519.1 | Complete certified run (`run_certified_board.py`) |
| 62 | 31 | 119,984 | 99,772 | 37,571 | 37,571 | 13.09 | 4,896.0 | 3,066.6 | Complete certified run (`run_certified_board.py`) |
| 64 | 32 | 135,116 | 96,115 | 60,407 | 60,407 | 22.58 | 3,407.8 | 1,953.9 | Complete certified run (`run_certified_board.py`) |
| **Total** | | **728,182** | **552,727** | **290,542** | **290,542** | **80.34** | **4,896.0** | **3,066.6** | **0 errors, 0 SAT, 100% DRAT verified** |

## Remaining limits

The raw DRAT proofs (`80.34 GB` cumulative across all eight boards) were temporary and deleted immediately after each leaf passed `drat-trim`; every formula can be regenerated deterministically from the pinned source and re-checked. Automated verification and AI adversarial review do not constitute independent human mathematical verification of the written proofs or C++ encoder.
