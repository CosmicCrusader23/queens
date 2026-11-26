# Mathematical reductions and branch tags

The proofs are in Sections 2–6 of the [manuscript](https://github.com/CosmicCrusader23/queens/blob/main/paper/paper.tex). Sections 2–3 define the extreme empty-line frame and prove the parity identities. Section 4 derives all algebraic exclusions. Section 5 gives the case conditions. Section 6 proves the counter bounds and the coverage of the generated formulas.

The reduction starts with an even board $n=2k$ and a hypothetical dominating set of size $k$. Any smaller dominating set can be enlarged to that size. A canonical case is the lexicographic minimum of the four margins and four occupied-line deficits under the eight dihedral transforms. Every dominating $k$-set survives the margin, deficit, and parity tests in at least one canonical representative.

A tag consists of the eight canonical case entries followed by `_b`, a branch number, and five integer parameters. Unused parameters are zero.

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

The special frame $(0,1,0,1)$ permits all queens to lie on odd-odd squares in branches 8 and 12. Branch 12 can overlap some general branches. The proof uses a covering family. Branch representatives and lex constraints use only the case stabilizer and the actions on their parameters proved in the manuscript.

The Boolean proof checker tests refutations of individual CNFs. The mathematical coverage theorem and the encoder connect those CNFs to queen domination. The [audit report](REVIEW.md) records which tests were rerun during revision.
