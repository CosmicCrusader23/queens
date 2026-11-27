# Adversarial review and audit record

## Corrections and evidence ($Q_{50}\text{--}Q_{54}$ baseline and $Q_{56}\text{--}Q_{64}$ extension)

| Finding / Check | Resolution and evidence |
| --- | --- |
| Uncontrolled cross-machine speedup ratios in early draft. | Removed uncontrolled comparison tables and projections; kept controlled single-machine $n=26$ benchmarks (`results/benchmark-overhead-n26.json`, `results/benchmark-pruning-n26.json`) and separated measured data from scaling discussion. |
| Disjointness wording and parity-counter caps. | Section 6 proves the covering theorem (`thm:coverage`), the parity-counter upper bounds (`lem:caps`, including $k=2,3$), and auxiliary variable extensions. |
| Border-waste deduplication for an odd-sided square with even $k$ and signed offset for a single diagonal deficit. | Sections 5.4–5.5 derive the offsets and waste counts from equations (2)–(4). For an odd-sided zero-deficit square with even $k$, the two side wastes sum to the border waste in color $q$ when its box diagonal is occupied. |
| Scheduling-only prioritization of `(0,1,0,1)` leaves (`b8`, `b3`, `b12`) and failure-artifact preservation added for $Q_{56}\text{--}Q_{64}$. | Verified zero change to generated formulas: `check_small_boards.py` passes on $Q_4, Q_6, Q_8$ with and without SBP, and all 74,155 regenerated DIMACS SHA-256 hashes on archived $Q_{50}\text{--}Q_{54}$ logs match bit-for-bit. |
| Resumability and resource monitoring for $Q_{56}\text{--}Q_{64}$. | Executed sequentially via `scripts/run_certified_board.py` with 0.5 s polling of free disk, RSS, swap, and macOS VM pressure; no run triggered suspension (`min_free_disk_gb >= 21.06 GB`, `max_vm_pressure_level <= 2`, zero resumed segments needed). |
| Independent domination on all eight boards. | Corollary 1.2 combines the ordinary lower bounds with the independent placements on p. 11 of Östergård and Weakley (2001). Weakley (2022), Proposition 21, also records the upper bounds. A direct coordinate check confirmed all eight placements. Appendix A gives another independent witness on $Q_{60}$ and checks placements for five smaller values already reported by Neuhaus (§2.4, pp. 17–18). [Östergård and Weakley](https://doi.org/10.37236/1573) · [Weakley](https://doi.org/10.37236/10617). |
| Prior work and attribution. | The cited literature gives the two-value bounds on the eight boards. The manuscript credits Neuhaus for parity-sensitive frame counts, Östergård and Weakley for the moment identities, and the OEIS record for its attribution of *Hint and Conquer* to Taha Rostami. [Neuhaus](https://kups.ub.uni-koeln.de/2916/) · [OEIS A075458](https://oeis.org/A075458/internal). |
| Proposed constraints for larger boards. | Section 7.4 derives the monochromatic exclusion from Weakley’s Theorems 17–18, Eq. (11), Proposition 19, and the negative $F(k)$ bounds. Proposition 11 restricts the counts in bichromatic formulas. The existing margin bound already implies $h\ge k$ for nonsquare frames with both sides even and $w>h$. The recorded lower-bound runs use the pinned encoding in Sections 2–6. [Weakley](https://doi.org/10.37236/10617). |

## Mathematical completeness audit for $n = 50, 52, 54, 56, 58, 60, 62, 64$

- **Uniform applicability ($n = 2k \ge 4$):** All frame definitions, $D_4$ parity actions, color-split identities (Theorem 3.1), algebraic exclusions (Lemmas 4.1–4.4), branch decompositions (Section 5), parity-counter bounds (Lemma 6.1), and the coverage theorem (Theorem 6.2) hold verbatim for every even $n = 2k \ge 4$ without modification for $n \in \{56, 58, 60, 62, 64\}$.
- **Integer and solver bounds:** For $n=64$ ($k=32$), the quadratic moment right-hand side $4\sum_{i=0}^{31} i^2=41\,664$ fits in a 64-bit integer. Difference and sum indices lie in $[-63,63]$ and $[0,126]$, respectively. Across the archived runs the largest recorded counts are 27,290 variables and 113,561 clauses. The longest checker invocation took 2,544.16 s on $Q_{62}$ with a timeout of 1,000,000 s.
- **Small boards:** `scripts/check_small_boards.py` verifies exact agreement with exhaustive bit-mask enumeration ($12$, $4$, and $0$ dominating $n/2$-sets for $n=4,6,8$) both with and without within-case lex constraints.
- **Upper bounds:** All 17 stored placements ($Q_{32}, Q_{34}, \dots, Q_{64}$) pass direct full-board coverage and independence checks in `scripts/verify_certificate.py`, and all coordinate lists printed in the manuscript match `results/even_upper_witnesses.json` verbatim.

## Complete computational verification coverage

| Board | Tested $k$ | Canonical cases | Pruned | Verified leaves | Matching SHA-256 hashes | DRAT (GB) | Peak RSS (MiB) | Peak temp (MB) | Log provenance |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 50 | 25 | 54,626 | 44,288 | 19,090 | 19,090 | 2.69 | — | — | Pinned encoder (`7872bd98`), bit-identical hashes |
| 52 | 26 | 63,119 | 43,354 | 30,814 | 30,814 | 6.21 | — | — | Pinned encoder (`7872bd98`), bit-identical hashes |
| 54 | 27 | 72,268 | 59,164 | 24,251 | 24,251 | 4.35 | — | — | Pinned encoder (`7872bd98`), bit-identical hashes |
| 56 | 28 | 82,695 | 57,564 | 39,144 | 39,144 | 9.53 | 929.2 | 208.0 | Complete certified run (`run_certified_board.py`) |
| 58 | 29 | 93,870 | 77,479 | 30,409 | 30,409 | 7.23 | 1,769.5 | 683.0 | Complete certified run (`run_certified_board.py`) |
| 60 | 30 | 106,504 | 74,991 | 48,856 | 48,856 | 14.67 | 1,335.3 | 519.2 | Complete certified run (`run_certified_board.py`) |
| 62 | 31 | 119,984 | 99,772 | 37,571 | 37,571 | 13.09 | 4,896.0 | 3,066.6 | Complete certified run (`run_certified_board.py`) |
| 64 | 32 | 135,116 | 96,115 | 60,407 | 60,407 | 22.58 | 3,407.8 | 1,953.9 | Complete certified run (`run_certified_board.py`) |
| **Total** | | **728,182** | **552,727** | **290,542** | **290,542** | **80.34** | **4,896.0** | **3,066.6** | **0 errors, 0 SAT, 100% DRAT verified** |

## Remaining limits

The raw DRAT proofs totaled 80.34 GB and were deleted after each proof passed `drat-trim`. The data in `results/` record the original checks. The latest manuscript review repeated the formula-hash audit and witness checks. Regeneration and fresh checking of the full proof set require a separate run using the pinned code.
