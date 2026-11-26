# Mathematical reductions and branch tags

The complete mathematical proofs are given in Sections 2–6 of the [manuscript](https://github.com/CosmicCrusader23/queens/blob/main/paper/paper.tex). Sections 2–3 define the extreme empty-line frame and prove the chessboard-parity incidence identities. Section 4 derives all four algebraic exclusions (`NS-1`, `NS-2`, `NS-3`, `SQ-RC-Odd`). Section 5 establishes the exhaustive color-split branch decomposition. Section 6 proves the parity-counter bounds and the coverage theorem for all even $n = 2k \ge 4$ (applied here to $n = 50, 52, 54, 56, 58, 60, 62, 64$).

The reduction starts with an even board $n=2k\ge4$ and a hypothetical dominating set of size $k$. Any smaller dominating set can be enlarged to size $k$. A canonical case is the lexicographic minimum of the four margins and four occupied-line deficits under the eight dihedral transforms. Every dominating $k$-set survives the margin, deficit, and parity tests in at least one canonical representative.

A branch tag consists of the eight canonical case entries followed by `_b`, a branch number, and five integer parameters (unused parameters are zero):

| Branch | Condition | Parameters in order |
| ---: | --- | --- |
| 0 | Nonsquare, odd longer side | — |
| 1 | Nonsquare, even longer and odd shorter side | Number $z_0$ of color-zero queens |
| 2 | Nonsquare, both sides even | Difference $\Delta$ of the two parity counts |
| 3 | Square, total deficit two | — |
| 4 | Odd-sided square with one row or column deficit | — |
| 5 | Even-sided square with one row or column deficit | Difference $\Delta$ of the two parity counts |
| 6 | Odd-sided square with one diagonal deficit | Color of the repeated diagonal |
| 7 | Even-sided square with one diagonal deficit | Repeated diagonal color and whether the count in the color opposite to that of the corners must be positive |
| 8 | Monochromatic reduced formula with one diagonal deficit | — |
| 9 | Odd-sided zero-deficit square, odd $k$ | Smaller color |
| 10 | Odd-sided zero-deficit square, even $k$ | Box-diagonal occupancy bits $(u_0,v_0)$ |
| 11 | Even-sided zero-deficit square | $(\Delta_V,\Delta_H,u_0,v_0)$ and whether the count in the color opposite to that of the corners must be positive |
| 12 | Monochromatic reduced zero-deficit formula | — |

The special frame $(0,1,0,1)$ permits all queens to lie on odd-odd squares in branches 8 and 12. Branch 12 can overlap some general branches; the soundness proof uses a covering family. Branch representatives and lex constraints use only the case stabilizer and the actions on their parameters proved in the manuscript.

### Scheduling invariance

In `src/queen_prover.cpp`, work items on the special frame $(0,1,0,1)$ in branches 8, 3, and 12 are assigned top queue priorities (135, 130, and 125, respectively) so that long-running monochromatic or near-monochromatic proofs start immediately at $t=0$ and overlap with the rest of the queue. Priority affects only dispatch order and alters neither the set of generated tags nor any generated CNF formula; regenerating all 74,155 DIMACS SHA-256 fingerprints on $Q_{50}\text{--}Q_{54}$ confirms bit-for-bit identity with the earlier pinned logs.

### Trust base

Unmodified `drat-trim` checks the UNSAT refutations of individual CNF files. The mathematical coverage theorem (Sections 2–6 of the manuscript) and the encoder (`src/queen_prover.cpp`) connect those CNFs to queen domination, and `scripts/verify_certificate.py` independently re-enumerates canonical cases, pruning counts, and branch tags and verifies all 290,542 regenerated SHA-256 digests and all 17 upper-bound witnesses. See [REVIEW.md](REVIEW.md) for the complete audit record.
