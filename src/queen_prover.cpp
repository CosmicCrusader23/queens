// Frame reduction and DRAT checking for even square boards, k = n/2.
//   - Joint D4 canonicalization of (frame, deficit_vector)
//   - Algebraic pruning:
//       NS-1 (margin reachability & Hall triangle row capacity)
//       NS-2 (odd-width parity elimination for k odd)
//       NS-3 (even-width, odd-height, d=2 parity elimination for k odd)
//       SQ-RC-Odd (odd-width square (0,1,0,0)/(1,0,0,0) parity elimination for k odd)
//   - Complete Color-Split Frame & Side Identities (Theorems F_q, V_q, H_q) across
//     ALL categories: NS (odd-h & even-h), SQ_DEF2, SQ_DEF1_RC, SQ_DEF1_TS, SQ0!
//   - Direct binary/ternary zero-waste elimination & dead-variable pruning
//   - Zero-waste border deduplication inside (F) Totalizer
//   - Exact monochromatic k x k subproblem + pure-diagonal moment identities (Lemma M)
//   - Stabilizer symmetry breaking (SBP) restricted to branch-preserving symmetries
//   - Native libcadical.a C++ API + direct posix_spawn drat-trim verification (zero /bin/sh)

#include "cadical.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t g_signal_stop = 0;

void handle_stop_signal(int) {
    g_signal_stop = 1;
}

struct Case {
    int ml, mr, mb, mt;
    int dR, dC, dT, dS;

    auto tuple() const {
        return std::array<int, 8>{ml, mr, mb, mt, dR, dC, dT, dS};
    }
    bool operator==(const Case& o) const { return tuple() == o.tuple(); }
    bool operator<(const Case& o) const { return tuple() < o.tuple(); }
};

std::array<Case, 8> case_images(const Case& c) {
    return {{
        {c.ml, c.mr, c.mb, c.mt, c.dR, c.dC, c.dT, c.dS}, // 0: id
        {c.mr, c.ml, c.mb, c.mt, c.dR, c.dC, c.dS, c.dT}, // 1: x -> m-x
        {c.ml, c.mr, c.mt, c.mb, c.dR, c.dC, c.dS, c.dT}, // 2: y -> m-y
        {c.mr, c.ml, c.mt, c.mb, c.dR, c.dC, c.dT, c.dS}, // 3: half-turn
        {c.mb, c.mt, c.ml, c.mr, c.dC, c.dR, c.dT, c.dS}, // 4: transpose
        {c.mt, c.mb, c.ml, c.mr, c.dC, c.dR, c.dS, c.dT}, // 5: (m-y, x)
        {c.mb, c.mt, c.mr, c.ml, c.dC, c.dR, c.dS, c.dT}, // 6: (y, m-x)
        {c.mt, c.mb, c.mr, c.ml, c.dC, c.dR, c.dT, c.dS}, // 7: anti-transpose
    }};
}

std::vector<int> case_stabilizer(const Case& c) {
    auto imgs = case_images(c);
    std::vector<int> stab;
    for (int g = 0; g < 8; ++g) {
        if (imgs[g] == c) stab.push_back(g);
    }
    return stab;
}

bool admissible_even(int n, int k, const Case& c) {
    if (c.dR < 0 || c.dC < 0 || c.dT < 0 || c.dS < 0) return false;
    int dm = k - 1;
    if (c.dR > dm || c.dC > dm || c.dT > dm || c.dS > dm) return false;
    if (c.ml + c.mr > k - c.dC || c.mb + c.mt > k - c.dR) return false;
    int w = n - 1 - c.ml - c.mr;
    int h = n - 1 - c.mb - c.mt;
    if (w < 1 || h < 1) return false;
    if (c.dR + c.dC + c.dT + c.dS > 2) return false;
    int sq = (w == h) ? 2 : 0;
    if (w >= h && c.dT + c.dS + 2 * c.dR > sq) return false;
    if (h >= w && c.dT + c.dS + 2 * c.dC > sq) return false;
    return true;
}

// Returns:
//   0 = survives
//   1 = NS-1 (margin reachability / Hall triangle)
//   2 = NS-2 (NS odd long-side parity, k odd)
//   3 = NS-3 (NS even long-side, odd short-side, d=2, k odd)
//   4 = SQ-RC-Odd (SQ odd-w, (0,1,0,0)/(1,0,0,0), k odd)
int algebraic_prune(int n, int k, const Case& c) {
    int w = n - 1 - c.ml - c.mr;
    int h = n - 1 - c.mb - c.mt;
    if (w == h) {
        if ((w & 1) && (k & 1) && (c.dR + c.dC == 1) && (c.dT == 0 && c.dS == 0)) {
            return 4; // SQ-RC-Odd: 2*z0 = k impossible for odd k!
        }
        return 0;
    }
    int ml = c.ml, mr = c.mr, mb = c.mb, mt = c.mt;
    int d_long = c.dC;
    if (h > w) {
        std::swap(ml, mb);
        std::swap(mr, mt);
        std::swap(w, h);
        d_long = c.dR;
    }
    // Now w > h, d_short = dT = dS = 0, waste_long = 0.
    if ((w & 1) && (k & 1)) return 2; // NS-2
    if (!(w & 1) && (h & 1) && (k & 1) && d_long == 2) return 3; // NS-3
    // NS-1:
    if (ml > h / 2 || mr > h / 2) return 1;
    if (ml + mr + mb + mt > k) return 1;
    int mx_m = std::max(ml, mr);
    for (int r = 1; r <= mx_m; ++r) {
        int need = std::max(0, ml - r + 1) + std::max(0, mr - r + 1);
        if (need > h + 1 - 2 * r) return 1;
    }
    if (mb > 0 && (2 * mb > w || w > 2 * h + 2)) return 1;
    if (mt > 0 && (2 * mt > w || w > 2 * h + 2)) return 1;
    return 0;
}

enum class BranchKind : uint8_t {
    NS_ODD_LONG = 0,     // Non-square, long side odd (only k even survives; z0=z1=k/2)
    NS_EVEN_ODD = 1,     // Non-square, long side even, short side odd: p0 = z0 (exact color split!)
    NS_EVEN_EVEN = 2,    // Non-square, long side even, short side even: p0 = Delta in [0, 2-d_long]
    SQ_DEF2 = 3,         // Square, sum deficits == 2 (u0=v0=1, zero-waste everywhere)
    SQ_DEF1_RC_ODD = 4,  // Square odd w, (0,1,0,0)/(1,0,0,0), k even (z0=z1=k/2)
    SQ_DEF1_RC_EVEN = 5, // Square even w, (0,1,0,0)/(1,0,0,0), p0 = Delta in {0, 1}
    SQ_DEF1_TS_ODD = 6,  // Square odd w, (0,0,0,1)/(0,0,1,0), p0 = q_def in {0, 1}
    SQ_DEF1_TS_EVEN = 7, // Square even w, (0,0,0,1)/(0,0,1,0), p0 = q_def, p1 = bichrom_only
    SQ_DEF1_TS_MONO = 8, // Square (0,1,0,1), (0,0,0,1)/(0,0,1,0), z_bar == 0 monochromatic on k x k + 1st moment
    SQ0_ODD_W_ODD_K = 9, // Square odd w, k odd, d=0, p0 = q_small
    SQ0_ODD_W_EVEN_K = 10,// Square odd w, k even, d=0, p0 = u0v, p1 = v0v
    SQ0_EVEN_W = 11,     // Square even w, d=0, p0 = dV, p1 = dH, p2 = u0v, p3 = v0v, p4 = bichrom_only
    SQ0_MONO_M = 12      // Square (0,1,0,1), d=0, z_bar == 0 monochromatic on k x k + Lemma M
};

struct WorkItem {
    Case c;
    BranchKind kind;
    int p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0;
    int priority = 0;

    std::string tag() const {
        std::ostringstream os;
        os << c.ml << "_" << c.mr << "_" << c.mb << "_" << c.mt << "_"
           << c.dR << "_" << c.dC << "_" << c.dT << "_" << c.dS << "_b"
           << static_cast<int>(kind) << "_" << p0 << "_" << p1 << "_" << p2 << "_" << p3 << "_" << p4;
        return os.str();
    }
};

std::vector<WorkItem> expand_case(int n, int k, const Case& c) {
    std::vector<WorkItem> out;
    int w = n - 1 - c.ml - c.mr;
    int h = n - 1 - c.mb - c.mt;
    int sd = c.dR + c.dC + c.dT + c.dS;
    auto stab = case_stabilizer(c);
    auto has_sym = [&](std::initializer_list<int> gs) {
        for (int g : gs) {
            if (std::find(stab.begin(), stab.end(), g) != stab.end()) return true;
        }
        return false;
    };

    if (w != h) {
        int l_side = (w > h) ? w : h;
        int s_side = (w > h) ? h : w;
        int d_long = (w > h) ? c.dC : c.dR;
        if (l_side & 1) {
            // Only possible for k even (or if --no-alg-prune); single case!
            out.push_back({c, BranchKind::NS_ODD_LONG, 0, 0, 0, 0, 0, 25 - d_long});
        } else if (s_side & 1) {
            // l_side even, s_side odd: 2*z0 in [k + d_long - 2, k - d_long + 2]
            int z_lo = (k + d_long - 2 + 1) / 2; // ceil((k + d_long - 2) / 2)
            int z_hi = (k - d_long + 2) / 2;     // floor((k - d_long + 2) / 2)
            // Note: color-flipping sym in stab (1 or 2) maps z0 <-> k - z0!
            bool flips_color = has_sym({1, 2, 5, 6});
            for (int z0 = z_lo; z0 <= z_hi; ++z0) {
                if (flips_color && z0 > k - z0) continue;
                out.push_back({c, BranchKind::NS_EVEN_ODD, z0, 0, 0, 0, 0, 35 - 5 * d_long});
            }
        } else {
            // Both l_side and s_side even! Delta in [0, 2 - d_long]
            for (int del = 0; del <= 2 - d_long; ++del) {
                out.push_back({c, BranchKind::NS_EVEN_EVEN, del, 0, 0, 0, 0, 30 - 5 * d_long + (del == 1 ? 5 : 0)});
            }
        }
        return out;
    }

    if (sd == 2) {
        bool mono_frame = (c.ml == 0 && c.mr == 1 && c.mb == 0 && c.mt == 1);
        out.push_back({c, BranchKind::SQ_DEF2, 0, 0, 0, 0, 0, mono_frame ? 130 : 15});
        return out;
    }

    if (sd == 1) {
        if (c.dR == 1 || c.dC == 1) {
            if (w & 1) {
                out.push_back({c, BranchKind::SQ_DEF1_RC_ODD, 0, 0, 0, 0, 0, 40});
            } else {
                out.push_back({c, BranchKind::SQ_DEF1_RC_EVEN, 0, 0, 0, 0, 0, 50});
                out.push_back({c, BranchKind::SQ_DEF1_RC_EVEN, 1, 0, 0, 0, 0, 50});
            }
        } else {
            // dT == 1 or dS == 1
            if (w & 1) {
                out.push_back({c, BranchKind::SQ_DEF1_TS_ODD, 0, 0, 0, 0, 0, 55});
                out.push_back({c, BranchKind::SQ_DEF1_TS_ODD, 1, 0, 0, 0, 0, 55});
            } else {
                int q_star = (c.ml + c.mb) & 1;
                bool mono_possible = (c.ml == 0 && c.mr == 1 && c.mb == 0 && c.mt == 1);
                if (mono_possible) {
                    out.push_back({c, BranchKind::SQ_DEF1_TS_MONO, 0, 0, 0, 0, 0, 135});
                    out.push_back({c, BranchKind::SQ_DEF1_TS_EVEN, q_star, 1, 0, 0, 0, 80});
                } else {
                    out.push_back({c, BranchKind::SQ_DEF1_TS_EVEN, q_star, 0, 0, 0, 0, 75});
                }
                out.push_back({c, BranchKind::SQ_DEF1_TS_EVEN, 1 - q_star, 0, 0, 0, 0, 70});
            }
        }
        return out;
    }

    // sd == 0 (SQ0!)
    if (w & 1) {
        if (k & 1) {
            out.push_back({c, BranchKind::SQ0_ODD_W_ODD_K, 0, 0, 0, 0, 0, 90});
            if (!has_sym({1, 2, 5, 6})) {
                out.push_back({c, BranchKind::SQ0_ODD_W_ODD_K, 1, 0, 0, 0, 0, 90});
            }
        } else {
            out.push_back({c, BranchKind::SQ0_ODD_W_EVEN_K, 1, 1, 0, 0, 0, 120});
            out.push_back({c, BranchKind::SQ0_ODD_W_EVEN_K, 1, 0, 0, 0, 0, 70});
            if (!has_sym({1, 2, 5, 6})) {
                out.push_back({c, BranchKind::SQ0_ODD_W_EVEN_K, 0, 1, 0, 0, 0, 70});
            }
            out.push_back({c, BranchKind::SQ0_ODD_W_EVEN_K, 0, 0, 0, 0, 0, 60});
        }
    } else {
        bool mono_possible = (c.ml == 0 && c.mr == 1 && c.mb == 0 && c.mt == 1);
        if (mono_possible) {
            out.push_back({c, BranchKind::SQ0_MONO_M, 0, 0, 0, 0, 0, 125});
            out.push_back({c, BranchKind::SQ0_EVEN_W, 0, 0, 1, 1, 1, 115});
        } else {
            out.push_back({c, BranchKind::SQ0_EVEN_W, 0, 0, 1, 1, 0, 105});
        }
        out.push_back({c, BranchKind::SQ0_EVEN_W, 1, 0, 1, 1, 0, 95});
        if (!has_sym({4, 7})) {
            out.push_back({c, BranchKind::SQ0_EVEN_W, 0, 1, 1, 1, 0, 95});
        }
        out.push_back({c, BranchKind::SQ0_EVEN_W, 1, 1, 1, 1, 0, 92});
        out.push_back({c, BranchKind::SQ0_EVEN_W, 0, 0, 1, 0, 0, 65});
        out.push_back({c, BranchKind::SQ0_EVEN_W, 0, 0, 0, 1, 0, 65});
        out.push_back({c, BranchKind::SQ0_EVEN_W, 0, 0, 0, 0, 0, 60});
    }
    return out;
}

struct CnfBuilder {
    int nv = 0;
    std::vector<int> lits;
    size_t n_clauses = 0;

    int new_var() { return ++nv; }

    void add_clause(std::initializer_list<int> cl) {
        for (int l : cl) lits.push_back(l);
        lits.push_back(0);
        ++n_clauses;
    }

    void add_clause_vec(const std::vector<int>& cl) {
        for (int l : cl) lits.push_back(l);
        lits.push_back(0);
        ++n_clauses;
    }

    void atmost_seq(const std::vector<int>& xs, int bound) {
        int m = static_cast<int>(xs.size());
        if (bound < 0) { add_clause({}); return; }
        if (bound >= m) return;
        if (bound == 0) {
            for (int l : xs) add_clause({-l});
            return;
        }
        if (bound == 1 && m <= 10) {
            for (int i = 0; i < m; ++i)
                for (int j = i + 1; j < m; ++j)
                    add_clause({-xs[i], -xs[j]});
            return;
        }
        std::vector<std::vector<int>> s(m - 1, std::vector<int>(bound));
        for (int i = 0; i < m - 1; ++i)
            for (int j = 0; j < bound; ++j)
                s[i][j] = new_var();
        add_clause({-xs[0], s[0][0]});
        for (int j = 1; j < bound; ++j) add_clause({-s[0][j]});
        for (int i = 1; i < m - 1; ++i) {
            add_clause({-xs[i], s[i][0]});
            add_clause({-s[i - 1][0], s[i][0]});
            for (int j = 1; j < bound; ++j) {
                add_clause({-xs[i], -s[i - 1][j - 1], s[i][j]});
                add_clause({-s[i - 1][j], s[i][j]});
            }
            add_clause({-xs[i], -s[i - 1][bound - 1]});
        }
        add_clause({-xs[m - 1], -s[m - 2][bound - 1]});
    }

    std::vector<int> totalizer_ub_rec(const int* xs, int len, int ub) {
        if (len == 0) return {};
        if (len == 1) return {xs[0]};
        int mid = len / 2;
        auto L = totalizer_ub_rec(xs, mid, ub);
        auto R = totalizer_ub_rec(xs + mid, len - mid, ub);
        int out_len = std::min(static_cast<int>(L.size() + R.size()), ub);
        std::vector<int> out(out_len);
        for (int i = 0; i < out_len; ++i) out[i] = new_var();
        for (int i = 0; i < static_cast<int>(L.size()) && i < out_len; ++i)
            add_clause({-L[i], out[i]});
        for (int j = 0; j < static_cast<int>(R.size()) && j < out_len; ++j)
            add_clause({-R[j], out[j]});
        for (int i = 0; i < static_cast<int>(L.size()); ++i) {
            for (int j = 0; j < static_cast<int>(R.size()); ++j) {
                int idx = i + j + 1;
                if (idx < out_len) {
                    add_clause({-L[i], -R[j], out[idx]});
                } else if (idx == ub) {
                    add_clause({-L[i], -R[j]});
                }
            }
        }
        return out;
    }

    void atmost_tot(const std::vector<int>& xs, int bound) {
        int m = static_cast<int>(xs.size());
        if (bound < 0) { add_clause({}); return; }
        if (bound >= m) return;
        if (bound == 0) {
            for (int l : xs) add_clause({-l});
            return;
        }
        if (bound == 1 && m <= 10) {
            for (int i = 0; i < m; ++i)
                for (int j = i + 1; j < m; ++j)
                    add_clause({-xs[i], -xs[j]});
            return;
        }
        totalizer_ub_rec(xs.data(), m, bound);
    }

    std::vector<int> totalizer_both_rec(const int* xs, int len, int ub) {
        if (len == 0) return {};
        if (len == 1) {
            if (ub <= 0) { add_clause({-xs[0]}); return {}; }
            return {xs[0]};
        }
        int mid = len / 2;
        auto L = totalizer_both_rec(xs, mid, ub);
        auto R = totalizer_both_rec(xs + mid, len - mid, ub);
        int lsz = static_cast<int>(L.size());
        int rsz = static_cast<int>(R.size());
        int out_len = std::min(lsz + rsz, ub);
        std::vector<int> out(out_len);
        for (int i = 0; i < out_len; ++i) out[i] = new_var();
        for (int i = 0; i <= lsz; ++i) {
            for (int j = 0; j <= rsz; ++j) {
                int s = i + j;
                if (s > 0 && s <= out_len) {
                    std::vector<int> cl;
                    cl.push_back(out[s - 1]);
                    if (i > 0) cl.push_back(-L[i - 1]);
                    if (j > 0) cl.push_back(-R[j - 1]);
                    add_clause_vec(cl);
                } else if (s == ub + 1) {
                    std::vector<int> cl;
                    if (i > 0) cl.push_back(-L[i - 1]);
                    if (j > 0) cl.push_back(-R[j - 1]);
                    add_clause_vec(cl);
                }
                if (s < out_len) {
                    std::vector<int> cl;
                    cl.push_back(-out[s]);
                    if (i < lsz) cl.push_back(L[i]);
                    if (j < rsz) cl.push_back(R[j]);
                    add_clause_vec(cl);
                }
            }
        }
        return out;
    }

    void exactly_tot(const std::vector<int>& xs, int target) {
        int m = static_cast<int>(xs.size());
        if (target < 0 || target > m) { add_clause({}); return; }
        if (target == 0) { for (int l : xs) add_clause({-l}); return; }
        if (target == m) { for (int l : xs) add_clause({l}); return; }
        atmost_tot(xs, target);
        std::vector<int> neg(m);
        for (int i = 0; i < m; ++i) neg[i] = -xs[i];
        atmost_tot(neg, m - target);
    }

    void add_amo_line(const std::vector<int>& qs) {
        if (qs.size() <= 10) {
            for (size_t i = 0; i < qs.size(); ++i)
                for (size_t j = i + 1; j < qs.size(); ++j)
                    add_clause({-qs[i], -qs[j]});
        } else {
            atmost_seq(qs, 1);
        }
    }

    void add_modsum_target(const std::vector<std::pair<int, int>>& terms, int target, int mod) {
        std::vector<int> z(mod);
        for (int r = 0; r < mod; ++r) z[r] = new_var();
        add_clause({z[0]});
        for (int r = 1; r < mod; ++r) add_clause({-z[r]});
        for (auto [coef, lit] : terms) {
            int c = ((coef % mod) + mod) % mod;
            if (c == 0) continue;
            std::vector<int> z2(mod);
            for (int r = 0; r < mod; ++r) z2[r] = new_var();
            for (int r = 0; r < mod; ++r) {
                int r2 = (r + c) % mod;
                add_clause({-z[r], -lit, z2[r2]});
                add_clause({-z[r], lit, z2[r]});
                add_clause({-z2[r2], -lit, z[r]});
                add_clause({-z2[r], lit, z[r]});
            }
            z = std::move(z2);
        }
        int tmod = ((target % mod) + mod) % mod;
        for (int r = 0; r < mod; ++r) {
            if (r != tmod) add_clause({-z[r]});
        }
    }

    // Tie unary counts: sum(A) == sum(B) + offset (where offset >= 0, so sum(A) >= offset)
    void tie_unary_equal(const std::vector<int>& uA, const std::vector<int>& uB, int offset) {
        int a_sz = static_cast<int>(uA.size());
        int b_sz = static_cast<int>(uB.size());
        if (offset > a_sz) { add_clause({}); return; }
        if (offset > 0) add_clause({uA[offset - 1]});
        for (int z = 0; z < b_sz; ++z) {
            int az = z + offset;
            if (az < a_sz) {
                add_clause({-uA[az], uB[z]});
                add_clause({-uB[z], uA[az]});
            } else {
                add_clause({-uB[z]});
            }
        }
        if (b_sz + offset < a_sz) {
            add_clause({-uA[b_sz + offset]});
        }
    }

    void add_sbp_rc(int n, const std::vector<int>& oR, const std::vector<int>& oC,
                    const Case& c, const std::vector<int>& allowed_syms) {
        auto stab = case_stabilizer(c);
        int m = n - 1;
        std::vector<int> v;
        v.reserve(2 * n);
        for (int i = 0; i < n; ++i) v.push_back(oR[i]);
        for (int i = 0; i < n; ++i) v.push_back(oC[i]);

        for (int g : stab) {
            if (g == 0) continue;
            if (std::find(allowed_syms.begin(), allowed_syms.end(), g) == allowed_syms.end()) continue;
            std::vector<int> u;
            u.reserve(2 * n);
            auto push_r = [&](bool rev) {
                for (int i = 0; i < n; ++i) u.push_back(oR[rev ? (m - i) : i]);
            };
            auto push_c = [&](bool rev) {
                for (int i = 0; i < n; ++i) u.push_back(oC[rev ? (m - i) : i]);
            };
            if (g == 1) { push_r(false); push_c(true); }
            else if (g == 2) { push_r(true); push_c(false); }
            else if (g == 3) { push_r(true); push_c(true); }
            else if (g == 4) { push_c(false); push_r(false); }
            else if (g == 5) { push_c(false); push_r(true); }
            else if (g == 6) { push_c(true); push_r(false); }
            else if (g == 7) { push_c(true); push_r(true); }

            int e = 0;
            for (int i = 0; i < 2 * n; ++i) {
                if (v[i] == u[i]) continue;
                if (e == 0) add_clause({-v[i], u[i]});
                else add_clause({-e, -v[i], u[i]});
                if (i < 2 * n - 1) {
                    int e2 = new_var();
                    if (e == 0) {
                        add_clause({-v[i], -u[i], e2});
                        add_clause({v[i], u[i], e2});
                    } else {
                        add_clause({-e, -v[i], -u[i], e2});
                        add_clause({-e, v[i], u[i], e2});
                    }
                    e = e2;
                }
            }
        }
    }

    std::string write_dimacs_and_sha256(const std::string& path) const {
        std::string buf;
        buf.reserve(lits.size() * 6 + 64);
        buf += "p cnf " + std::to_string(nv) + " " + std::to_string(n_clauses) + "\n";
        char numbuf[16];
        bool first = true;
        for (int l : lits) {
            if (l == 0) {
                buf += " 0\n";
                first = true;
            } else {
                if (!first) buf.push_back(' ');
                first = false;
                auto result = std::to_chars(numbuf, numbuf + sizeof(numbuf), l);
                buf.append(numbuf, result.ptr);
            }
        }
        if (!path.empty()) {
            std::ofstream ofs(path, std::ios::binary);
            ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            ofs.close();
            if (!ofs) return {};
        }

        unsigned char hash[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(buf.data(), static_cast<CC_LONG>(buf.size()), hash);
        static const char* hex = "0123456789abcdef";
        std::string out(64, '0');
        for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i) {
            out[2 * i] = hex[hash[i] >> 4];
            out[2 * i + 1] = hex[hash[i] & 0xF];
        }
        return out;
    }
};

struct BuiltFormula {
    CnfBuilder cnf;
    std::vector<std::tuple<int, int, int>> q_vars;
    bool prefer_unsat_cfg = false;
};

BuiltFormula build_mono_kxk(int k, int dT, int dS, bool use_sbp) {
    // Monochromatic z_bar == 0 on (0,1,0,1): queens are a permutation on odd-odd {1,3,...,2k-1}^2
    // with dR=0, dC=0, and (dT, dS) in {(0,0), (0,1), (1,0)}.
    BuiltFormula bf;
    bf.prefer_unsat_cfg = true;
    auto& B = bf.cnf;

    std::vector<std::vector<int>> q(k, std::vector<int>(k));
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j) {
            q[i][j] = B.new_var();
            bf.q_vars.emplace_back(q[i][j], 2 * i + 1, 2 * j + 1);
        }
    int nT = 2 * k - 1;
    std::vector<int> oT(nT), oS(nT);
    for (int i = 0; i < nT; ++i) { oT[i] = B.new_var(); oS[i] = B.new_var(); }

    for (int i = 0; i < k; ++i) {
        std::vector<int> row(k), col(k);
        for (int j = 0; j < k; ++j) { row[j] = q[i][j]; col[j] = q[j][i]; }
        B.add_clause_vec(row); B.add_amo_line(row);
        B.add_clause_vec(col); B.add_amo_line(col);
    }
    for (int t = -(k - 1); t <= k - 1; ++t) {
        std::vector<int> qs;
        for (int i = 0; i < k; ++i) {
            int j = i + t;
            if (j >= 0 && j < k) qs.push_back(q[i][j]);
        }
        int o = oT[t + (k - 1)];
        for (int v : qs) B.add_clause({-v, o});
        std::vector<int> cl = {-o};
        cl.insert(cl.end(), qs.begin(), qs.end());
        B.add_clause_vec(cl);
        if (dT == 0) B.add_amo_line(qs);
    }
    for (int s = 1; s <= 2 * k - 1; ++s) {
        std::vector<int> qs;
        for (int i = 0; i < k; ++i) {
            int j = s - 1 - i;
            if (j >= 0 && j < k) qs.push_back(q[i][j]);
        }
        int o = oS[s - 1];
        for (int v : qs) B.add_clause({-v, o});
        std::vector<int> cl = {-o};
        cl.insert(cl.end(), qs.begin(), qs.end());
        B.add_clause_vec(cl);
        if (dS == 0) B.add_amo_line(qs);
    }
    B.exactly_tot(oT, k - dT);
    B.exactly_tot(oS, k - dS);

    for (int u = 0; u < k; ++u) {
        for (int v = 0; v < k; ++v) {
            int t = v - u;
            int s = u + v;
            if (s >= 1 && s <= 2 * k - 1) B.add_clause({oT[t + (k - 1)], oS[s - 1]});
            else B.add_clause({oT[t + (k - 1)]});
        }
    }
    // Transpose SBP on oT (always valid for (0,1,0,1) with (dT,dS) fixed since transpose preserves T and S!)
    if (use_sbp) {
        int e = 0;
        for (int t = 1; t <= k - 1; ++t) {
            int vt = oT[t + (k - 1)];
            int ut = oT[-t + (k - 1)];
            if (e == 0) B.add_clause({-vt, ut});
            else B.add_clause({-e, -vt, ut});
            if (t < k - 1) {
                int e2 = B.new_var();
                if (e == 0) {
                    B.add_clause({-vt, -ut, e2});
                    B.add_clause({vt, ut, e2});
                } else {
                    B.add_clause({-e, -vt, -ut, e2});
                    B.add_clause({-e, vt, ut, e2});
                }
                e = e2;
            }
        }
    }
    // Exact pure-diagonal moment identities (Lemma M):
    // When dT == 0: sum_{t in T} t == 0
    // When dS == 0: sum_{s in S} s == k^2
    // When dT == 0 && dS == 0: sum t^2 + sum (s-1)^2 == 4 * sum_{i=0}^{k-1} i^2
    if (dT == 0 && dS == 0) {
        long long sum_x2 = 0;
        for (int i = 0; i < k; ++i) sum_x2 += 1LL * i * i;
        std::vector<std::pair<int, int>> t_lin, s_lin, quad;
        for (int t = -(k - 1); t <= k - 1; ++t) {
            t_lin.emplace_back(t, oT[t + (k - 1)]);
            quad.emplace_back(t * t, oT[t + (k - 1)]);
        }
        for (int s = 1; s <= 2 * k - 1; ++s) {
            s_lin.emplace_back(s, oS[s - 1]);
            quad.emplace_back((s - 1) * (s - 1), oS[s - 1]);
        }
        for (int mod : {3, 4, 5, 7, 8, 9}) {
            B.add_modsum_target(t_lin, 0, mod);
            B.add_modsum_target(s_lin, static_cast<int>((1LL * k * k) % mod), mod);
            B.add_modsum_target(quad, static_cast<int>((4LL * sum_x2) % mod), mod);
        }
    } else if (dT == 0 && dS == 1) {
        std::vector<std::pair<int, int>> t_lin;
        for (int t = -(k - 1); t <= k - 1; ++t) t_lin.emplace_back(t, oT[t + (k - 1)]);
        for (int mod : {3, 4, 5, 7, 8}) B.add_modsum_target(t_lin, 0, mod);
    } else if (dT == 1 && dS == 0) {
        std::vector<std::pair<int, int>> s_lin;
        for (int s = 1; s <= 2 * k - 1; ++s) s_lin.emplace_back(s, oS[s - 1]);
        for (int mod : {3, 4, 5, 7, 8}) B.add_modsum_target(s_lin, static_cast<int>((1LL * k * k) % mod), mod);
    }
    // Exact border waste constraints on monochromatic {0..k-1}^2 when dT + dS == 1:
    // (0,0) forces oT[0] = 1 (u0 = 1), so waste_F <= 2, waste_V <= [oS[k-1]], waste_H <= [oS[k-1]]!
    if (dT + dS == 1) {
        int v0_lit = oS[(k - 1) - 1]; // s = k - 1 (valid for k >= 2)
        auto add_mono_waste = [&](int which_set) {
            std::vector<int> wl;
            if (which_set == 0) {
                // Diags with beta_Bd < 2: t = +(k-1), t = -(k-1) (beta=1); s = 2k-2 (beta=1); s = 2k-1 (beta=0, weight 2!)
                wl.push_back(oT[0]);           // t = -(k-1)
                wl.push_back(oT[2 * k - 2]);   // t = +(k-1)
                if (2 * k - 2 >= 1) wl.push_back(oS[(2 * k - 2) - 1]); // s = 2k - 2
                wl.push_back(oS[(2 * k - 1) - 1]); // s = 2k - 1 (x2)
                wl.push_back(oS[(2 * k - 1) - 1]);
            } else {
                // V or H: only s = 2k - 1 misses V and H!
                wl.push_back(oS[(2 * k - 1) - 1]);
            }
            auto emit_double = [&](int u, int v) {
                int t = v - u, s = u + v;
                if (s >= 1 && s <= 2 * k - 1) {
                    int f = B.new_var();
                    B.add_clause({-oT[t + (k - 1)], -oS[s - 1], f});
                    wl.push_back(f);
                }
            };
            if (which_set == 1 || which_set == 0) {
                for (int v = 0; v < k; ++v) { emit_double(0, v); emit_double(k - 1, v); }
            }
            if (which_set == 2) {
                for (int u = 0; u < k; ++u) { emit_double(u, 0); emit_double(u, k - 1); }
            } else if (which_set == 0) {
                for (int u = 1; u < k - 1; ++u) { emit_double(u, 0); emit_double(u, k - 1); }
            }
            if (which_set == 0) {
                B.atmost_tot(wl, 2);
            } else {
                wl.push_back(-v0_lit);
                B.atmost_tot(wl, 1);
            }
        };
        add_mono_waste(0);
        add_mono_waste(1);
        add_mono_waste(2);
    }
    return bf;
}

BuiltFormula build_work_item(int n, int k, const WorkItem& wi, bool use_sbp) {
    const Case& c = wi.c;
    if (wi.kind == BranchKind::SQ0_MONO_M) {
        return build_mono_kxk(k, 0, 0, use_sbp);
    }
    if (wi.kind == BranchKind::SQ_DEF1_TS_MONO) {
        return build_mono_kxk(k, c.dT, c.dS, use_sbp);
    }

    BuiltFormula bf;
    auto& B = bf.cnf;

    int a = c.ml, b = n - 1 - c.mr, cc = c.mb, d = n - 1 - c.mt;
    int w = b - a, h = d - cc;
    int u0_t = cc - a;
    int v0_s = a + d;

    std::vector<int> oR(n), oC(n);
    for (int y = 0; y < n; ++y) oR[y] = B.new_var();
    for (int x = 0; x < n; ++x) oC[x] = B.new_var();
    int nD = 2 * n - 1;
    std::vector<int> oT(nD), oS(nD);
    for (int i = 0; i < nD; ++i) oT[i] = B.new_var();
    for (int i = 0; i < nD; ++i) oS[i] = B.new_var();

    auto litT = [&](int t) { return oT[t + (n - 1)]; };
    auto litS = [&](int s) { return oS[s]; };
    int u0 = litT(u0_t);
    int v0 = litS(v0_s);

    for (int x = 0; x < n; ++x) if (x < a || x > b) B.add_clause({oC[x]});
    for (int y = 0; y < n; ++y) if (y < cc || y > d) B.add_clause({oR[y]});
    B.add_clause({-oC[a]}); B.add_clause({-oC[b]});
    B.add_clause({-oR[cc]}); B.add_clause({-oR[d]});

    auto diag_hits = [&](bool is_T, int idx, int which_set) {
        std::vector<std::pair<int, int>> pts;
        if (which_set == 1) {
            for (int x : {a, b}) {
                int y = is_T ? (x + idx) : (idx - x);
                if (y >= cc && y <= d) pts.emplace_back(x, y);
            }
        } else if (which_set == 2) {
            for (int y : {cc, d}) {
                int x = is_T ? (y - idx) : (idx - y);
                if (x >= a && x <= b) pts.emplace_back(x, y);
            }
        } else {
            for (int x : {a, b}) {
                int y = is_T ? (x + idx) : (idx - x);
                if (y >= cc && y <= d) pts.emplace_back(x, y);
            }
            for (int y : {cc, d}) {
                int x = is_T ? (y - idx) : (idx - y);
                if (x > a && x < b) pts.emplace_back(x, y);
            }
        }
        return pts;
    };

    auto nonneedy_lit = [&](int x, int y) -> int {
        bool colE = (x == a || x == b);
        bool rowE = (y == cc || y == d);
        if (colE && rowE) return 0;
        return colE ? oR[y] : oC[x];
    };

    bool zero_F[2] = {false, false};
    bool zero_V[2] = {false, false};
    bool zero_H[2] = {false, false};
    bool skip_global_TS_card = false;
    std::vector<int> allowed_sbp_syms = {1, 2, 3, 4, 5, 6, 7};

    // Helper vectors for parity splits
    auto get_T_col = [&](int col) {
        std::vector<int> v;
        for (int t = -(n - 1); t <= n - 1; ++t) if ((t & 1) == col) v.push_back(litT(t));
        return v;
    };
    auto get_S_col = [&](int col) {
        std::vector<int> v;
        for (int s = 0; s <= 2 * n - 2; ++s) if ((s & 1) == col) v.push_back(litS(s));
        return v;
    };
    auto get_emp_R_par = [&](int par) {
        std::vector<int> v;
        for (int y = cc; y <= d; ++y) if ((y & 1) == par) v.push_back(-oR[y]);
        return v;
    };
    auto get_emp_C_par = [&](int par) {
        std::vector<int> v;
        for (int x = a; x <= b; ++x) if ((x & 1) == par) v.push_back(-oC[x]);
        return v;
    };

    // Configure per BranchKind
    if (wi.kind == BranchKind::NS_ODD_LONG) {
        bool v_long = (w > h);
        int d_long = v_long ? c.dC : c.dR;
        if (v_long) zero_V[0] = zero_V[1] = true;
        else zero_H[0] = zero_H[1] = true;
        if (d_long == 2) zero_F[0] = zero_F[1] = true;
        if (k & 1) {
            // Impossible (only reachable with --no-alg-prune)
            B.add_clause({});
            return bf;
        }
        skip_global_TS_card = true;
        for (int col = 0; col < 2; ++col) {
            B.exactly_tot(get_T_col(col), k / 2);
            B.exactly_tot(get_S_col(col), k / 2);
        }
    } else if (wi.kind == BranchKind::NS_EVEN_ODD) {
        bf.prefer_unsat_cfg = true;
        bool v_long = (w > h);
        int d_long = v_long ? c.dC : c.dR;
        int z0 = wi.p0;
        int z1 = k - z0;
        int z_of[2] = {z0, z1};
        if (v_long) zero_V[0] = zero_V[1] = true;
        else zero_H[0] = zero_H[1] = true;
        skip_global_TS_card = true;
        for (int col = 0; col < 2; ++col) {
            B.exactly_tot(get_T_col(col), z_of[col]);
            B.exactly_tot(get_S_col(col), z_of[col]);
            if (2 * z_of[col] - k - d_long + 2 == 0) zero_F[col] = true;
        }
        // Tie exact empty row/col parity counts to z0, z1!
        if (v_long) {
            int pa = a & 1;
            // Color 0 on V is rows of parity pa; color 1 is rows of parity 1-pa
            B.exactly_tot(get_emp_R_par(pa), z0);
            B.exactly_tot(get_emp_R_par(1 - pa), z1);
        } else {
            int pc = cc & 1;
            B.exactly_tot(get_emp_C_par(pc), z0);
            B.exactly_tot(get_emp_C_par(1 - pc), z1);
        }
        allowed_sbp_syms = (z0 == z1) ? std::vector<int>{1, 2, 3, 4, 5, 6, 7} : std::vector<int>{3, 4, 7};
    } else if (wi.kind == BranchKind::NS_EVEN_EVEN) {
        bf.prefer_unsat_cfg = true;
        bool v_long = (w > h);
        int d_long = v_long ? c.dC : c.dR;
        int del = wi.p0; // Delta in [0, 2 - d_long]
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        int pa = a & 1, pc = cc & 1;
        if (v_long) zero_V[0] = zero_V[1] = true;
        else zero_H[0] = zero_H[1] = true;
        if (del == 0) zero_F[q_bar] = true;
        if (2 * del == 4 - 2 * d_long) zero_F[q_star] = true;

        // Tie z_bar = |T_bar| = |S_bar| = e_bar(short) = e_bar(long) + del!
        auto T_bar = get_T_col(q_bar);
        auto S_bar = get_S_col(q_bar);
        auto eR_bar = get_emp_R_par(1 - pc);
        auto eC_bar = get_emp_C_par(1 - pa);
        auto& e_short = v_long ? eR_bar : eC_bar; // sum == z_bar
        auto& e_long  = v_long ? eC_bar : eR_bar; // sum == z_bar - del
        int max_z = std::min({k - 2, (int)e_short.size(), (int)e_long.size() + del});
        if (max_z < del) { B.add_clause({}); return bf; }
        auto uT = B.totalizer_both_rec(T_bar.data(), (int)T_bar.size(), max_z);
        auto uS = B.totalizer_both_rec(S_bar.data(), (int)S_bar.size(), max_z);
        auto uSh = B.totalizer_both_rec(e_short.data(), (int)e_short.size(), max_z);
        auto uLg = B.totalizer_both_rec(e_long.data(), (int)e_long.size(), max_z - del);
        B.tie_unary_equal(uT, uS, 0);
        B.tie_unary_equal(uT, uSh, 0);
        B.tie_unary_equal(uT, uLg, del);
        allowed_sbp_syms = {3, 4, 7};
    } else if (wi.kind == BranchKind::SQ_DEF2) {
        B.add_clause({u0}); B.add_clause({v0});
        zero_F[0] = zero_F[1] = true;
        zero_V[0] = zero_V[1] = true;
        zero_H[0] = zero_H[1] = true;
    } else if (wi.kind == BranchKind::SQ_DEF1_RC_ODD) {
        B.add_clause({u0}); B.add_clause({v0});
        if (c.dR == 1) zero_V[0] = zero_V[1] = true;
        else zero_H[0] = zero_H[1] = true;
        if (k & 1) { B.add_clause({}); return bf; }
        skip_global_TS_card = true;
        for (int col = 0; col < 2; ++col) {
            B.exactly_tot(get_T_col(col), k / 2);
            B.exactly_tot(get_S_col(col), k / 2);
        }
    } else if (wi.kind == BranchKind::SQ_DEF1_RC_EVEN) {
        bf.prefer_unsat_cfg = true;
        int del = wi.p0; // 0 or 1
        bool is_dC = (c.dC == 1);
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        int pa = a & 1, pc = cc & 1;
        B.add_clause({u0}); B.add_clause({v0});
        if (is_dC) zero_H[0] = zero_H[1] = true;
        else zero_V[0] = zero_V[1] = true;
        if (del == 0) {
            zero_F[q_bar] = true;
            if (is_dC) zero_V[q_bar] = true; else zero_H[q_bar] = true;
        } else {
            zero_F[q_star] = true;
            if (is_dC) zero_V[q_star] = true; else zero_H[q_star] = true;
        }
        auto T_bar = get_T_col(q_bar);
        auto S_bar = get_S_col(q_bar);
        auto eR_bar = get_emp_R_par(1 - pc);
        auto eC_bar = get_emp_C_par(1 - pa);
        // For dC=1: z_bar = eC_bar = eR_bar + del
        // For dR=1: z_bar = eR_bar = eC_bar + del
        auto& e_eq  = is_dC ? eC_bar : eR_bar;
        auto& e_sub = is_dC ? eR_bar : eC_bar;
        int max_z = std::min({k - 1, (int)e_eq.size(), (int)e_sub.size() + del});
        if (max_z < del) { B.add_clause({}); return bf; }
        auto uT = B.totalizer_both_rec(T_bar.data(), (int)T_bar.size(), max_z);
        auto uS = B.totalizer_both_rec(S_bar.data(), (int)S_bar.size(), max_z);
        auto uEq = B.totalizer_both_rec(e_eq.data(), (int)e_eq.size(), max_z);
        auto uSub = B.totalizer_both_rec(e_sub.data(), (int)e_sub.size(), max_z - del);
        B.tie_unary_equal(uT, uS, 0);
        B.tie_unary_equal(uT, uEq, 0);
        B.tie_unary_equal(uT, uSub, del);
        allowed_sbp_syms = {3};
    } else if (wi.kind == BranchKind::SQ_DEF1_TS_ODD) {
        int q_def = wi.p0;
        int q_oth = 1 - q_def;
        bool is_dS = (c.dS == 1);
        skip_global_TS_card = true;
        int diag_of[2];
        diag_of[u0_t & 1] = u0;
        diag_of[v0_s & 1] = v0;
        if (k & 1) {
            int z_def = (k + 1) / 2, z_oth = (k - 1) / 2;
            B.exactly_tot(get_T_col(q_def), is_dS ? z_def : (z_def - 1));
            B.exactly_tot(get_S_col(q_def), is_dS ? (z_def - 1) : z_def);
            B.exactly_tot(get_T_col(q_oth), z_oth);
            B.exactly_tot(get_S_col(q_oth), z_oth);
            B.add_clause({diag_of[q_oth]});
            zero_F[q_oth] = zero_V[q_oth] = zero_H[q_oth] = true;
        } else {
            int zh = k / 2;
            B.exactly_tot(get_T_col(q_def), is_dS ? zh : (zh - 1));
            B.exactly_tot(get_S_col(q_def), is_dS ? (zh - 1) : zh);
            B.exactly_tot(get_T_col(q_oth), zh);
            B.exactly_tot(get_S_col(q_oth), zh);
            B.add_clause({diag_of[q_def]});
            zero_F[q_def] = zero_V[q_def] = zero_H[q_def] = true;
        }
        allowed_sbp_syms = {3, 4, 7};
    } else if (wi.kind == BranchKind::SQ_DEF1_TS_EVEN) {
        bf.prefer_unsat_cfg = true;
        int q_def = wi.p0, bichrom_only = wi.p1;
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        bool is_dS = (c.dS == 1);
        int pa = a & 1, pc = cc & 1;
        auto eR_bar = get_emp_R_par(1 - pc);
        auto eC_bar = get_emp_C_par(1 - pa);
        auto T_bar = get_T_col(q_bar);
        auto S_bar = get_S_col(q_bar);
        if (q_def == q_star) {
            B.add_clause({u0, v0});
            zero_F[q_bar] = zero_V[q_bar] = zero_H[q_bar] = true;
            int max_z = std::min({k - 2, (int)eR_bar.size(), (int)eC_bar.size()});
            auto uT = B.totalizer_both_rec(T_bar.data(), (int)T_bar.size(), max_z);
            auto uS = B.totalizer_both_rec(S_bar.data(), (int)S_bar.size(), max_z);
            auto uR = B.totalizer_both_rec(eR_bar.data(), (int)eR_bar.size(), max_z);
            auto uC = B.totalizer_both_rec(eC_bar.data(), (int)eC_bar.size(), max_z);
            B.tie_unary_equal(uT, uS, 0);
            B.tie_unary_equal(uT, uR, 0);
            B.tie_unary_equal(uT, uC, 0);
            if (bichrom_only) B.add_clause_vec(eR_bar);
        } else {
            B.add_clause({u0}); B.add_clause({v0});
            zero_F[q_star] = zero_V[q_star] = zero_H[q_star] = true;
            auto& bigL = is_dS ? T_bar : S_bar;
            auto& smlL = is_dS ? S_bar : T_bar;
            int max_e = std::min({k - 2, (int)eR_bar.size(), (int)eC_bar.size()});
            auto uBig = B.totalizer_both_rec(bigL.data(), (int)bigL.size(), max_e + 1);
            auto uSml = B.totalizer_both_rec(smlL.data(), (int)smlL.size(), max_e);
            auto uR = B.totalizer_both_rec(eR_bar.data(), (int)eR_bar.size(), max_e);
            auto uC = B.totalizer_both_rec(eC_bar.data(), (int)eC_bar.size(), max_e);
            B.tie_unary_equal(uBig, uSml, 1);
            B.tie_unary_equal(uSml, uR, 0);
            B.tie_unary_equal(uSml, uC, 0);
        }
        allowed_sbp_syms = {3, 4, 7};
    } else if (wi.kind == BranchKind::SQ0_ODD_W_ODD_K) {
        int q_small = wi.p0, q_big = 1 - q_small;
        skip_global_TS_card = true;
        B.exactly_tot(get_T_col(q_small), (k - 1) / 2);
        B.exactly_tot(get_S_col(q_small), (k - 1) / 2);
        B.exactly_tot(get_T_col(q_big), (k + 1) / 2);
        B.exactly_tot(get_S_col(q_big), (k + 1) / 2);
        B.add_clause({((u0_t & 1) == q_small) ? u0 : v0});
        zero_F[q_small] = zero_V[q_small] = zero_H[q_small] = true;
        allowed_sbp_syms = {3, 4, 7};
    } else if (wi.kind == BranchKind::SQ0_ODD_W_EVEN_K) {
        int u0v = wi.p0, v0v = wi.p1;
        skip_global_TS_card = true;
        B.add_clause({u0v ? u0 : -u0});
        B.add_clause({v0v ? v0 : -v0});
        for (int col = 0; col < 2; ++col) {
            B.exactly_tot(get_T_col(col), k / 2);
            B.exactly_tot(get_S_col(col), k / 2);
        }
        int val_col[2];
        val_col[u0_t & 1] = u0v;
        val_col[v0_s & 1] = v0v;
        for (int col = 0; col < 2; ++col) {
            if (val_col[col] == 0) zero_V[col] = zero_H[col] = true;
        }
        allowed_sbp_syms = (u0v == v0v) ? std::vector<int>{1, 2, 3, 4, 5, 6, 7} : std::vector<int>{3, 4, 7};
    } else if (wi.kind == BranchKind::SQ0_EVEN_W) {
        bf.prefer_unsat_cfg = true;
        int dV = wi.p0, dH = wi.p1, u0v = wi.p2, v0v = wi.p3, bichrom_only = wi.p4;
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        B.add_clause({u0v ? u0 : -u0});
        B.add_clause({v0v ? v0 : -v0});
        int pa = a & 1, pc = cc & 1;
        auto eR_bar = get_emp_R_par(1 - pc);
        auto eC_bar = get_emp_C_par(1 - pa);
        auto T_bar = get_T_col(q_bar);
        auto S_bar = get_S_col(q_bar);
        int max_z = std::min({k - 2 + std::max(dV, dH), (int)eR_bar.size() + dV, (int)eC_bar.size() + dH, k});
        if (max_z < std::max(dV, dH)) { B.add_clause({}); return bf; }
        auto uT = B.totalizer_both_rec(T_bar.data(), (int)T_bar.size(), max_z);
        auto uS = B.totalizer_both_rec(S_bar.data(), (int)S_bar.size(), max_z);
        auto uR = B.totalizer_both_rec(eR_bar.data(), (int)eR_bar.size(), max_z - dV);
        auto uC = B.totalizer_both_rec(eC_bar.data(), (int)eC_bar.size(), max_z - dH);
        B.tie_unary_equal(uT, uS, 0);
        B.tie_unary_equal(uT, uR, dV);
        B.tie_unary_equal(uT, uC, dH);
        if (bichrom_only) B.add_clause_vec(eR_bar);
        if (dV == 0) zero_V[q_bar] = true; else zero_V[q_star] = true;
        if (dH == 0) zero_H[q_bar] = true; else zero_H[q_star] = true;
        if (dV == 0 && dH == 0) zero_F[q_bar] = true;
        if (dV == 1 && dH == 1) zero_F[q_star] = true;
        if (u0v == 0 && v0v == 0) {
            zero_V[q_star] = zero_H[q_star] = true;
            B.add_clause({litS(a + cc)}); B.add_clause({litS(b + d)});
            B.add_clause({litT(d - a)});  B.add_clause({litT(cc - b)});
        } else if (u0v == 0 && v0v == 1) {
            B.add_clause({litS(a + cc)}); B.add_clause({litS(b + d)});
        } else if (u0v == 1 && v0v == 0) {
            B.add_clause({litT(d - a)});  B.add_clause({litT(cc - b)});
        }
        allowed_sbp_syms = (dV == dH) ? std::vector<int>{3, 4, 7} : std::vector<int>{3};
    }

    // Dead diagonal pruning
    std::vector<uint8_t> deadT(nD, 0), deadS(nD, 0);
    for (int t = -(n - 1); t <= n - 1; ++t) {
        int col = t & 1;
        if ((zero_V[col] && diag_hits(true, t, 1).empty()) ||
            (zero_H[col] && diag_hits(true, t, 2).empty()) ||
            (zero_F[col] && diag_hits(true, t, 0).size() < 2)) {
            deadT[t + (n - 1)] = 1;
            B.add_clause({-litT(t)});
        }
    }
    for (int s = 0; s <= 2 * n - 2; ++s) {
        int col = s & 1;
        if ((zero_V[col] && diag_hits(false, s, 1).empty()) ||
            (zero_H[col] && diag_hits(false, s, 2).empty()) ||
            (zero_F[col] && diag_hits(false, s, 0).size() < 2)) {
            deadS[s] = 1;
            B.add_clause({-litS(s)});
        }
    }

    // Live queen variables
    std::vector<std::vector<int>> byR(n), byC(n), byT(nD), byS(nD);
    for (int x = 0; x < n; ++x) {
        if (x == a || x == b) continue;
        for (int y = 0; y < n; ++y) {
            if (y == cc || y == d) continue;
            int t = y - x, s = x + y;
            if (deadT[t + (n - 1)] || deadS[s]) continue;
            int v = B.new_var();
            bf.q_vars.emplace_back(v, x, y);
            byR[y].push_back(v); byC[x].push_back(v);
            byT[t + (n - 1)].push_back(v); byS[s].push_back(v);
            B.add_clause({-v, oR[y]});
            B.add_clause({-v, oC[x]});
            B.add_clause({-v, litT(t)});
            B.add_clause({-v, litS(s)});
        }
    }

    for (int y = 0; y < n; ++y) {
        std::vector<int> cl = {-oR[y]};
        cl.insert(cl.end(), byR[y].begin(), byR[y].end());
        B.add_clause_vec(cl);
        if (c.dR == 0) B.add_amo_line(byR[y]);
    }
    for (int x = 0; x < n; ++x) {
        std::vector<int> cl = {-oC[x]};
        cl.insert(cl.end(), byC[x].begin(), byC[x].end());
        B.add_clause_vec(cl);
        if (c.dC == 0) B.add_amo_line(byC[x]);
    }
    for (int i = 0; i < nD; ++i) {
        if (!deadT[i]) {
            std::vector<int> cl = {-oT[i]};
            cl.insert(cl.end(), byT[i].begin(), byT[i].end());
            B.add_clause_vec(cl);
            if (c.dT == 0) B.add_amo_line(byT[i]);
        }
        if (!deadS[i]) {
            std::vector<int> cl = {-oS[i]};
            cl.insert(cl.end(), byS[i].begin(), byS[i].end());
            B.add_clause_vec(cl);
            if (c.dS == 0) B.add_amo_line(byS[i]);
        }
    }

    for (int x = a; x <= b; ++x) {
        for (int y = cc; y <= d; ++y) {
            int t = y - x, s = x + y;
            std::vector<int> cl;
            if (y != cc && y != d) cl.push_back(oR[y]);
            if (x != a && x != b) cl.push_back(oC[x]);
            if (!deadT[t + (n - 1)]) cl.push_back(litT(t));
            if (!deadS[s]) cl.push_back(litS(s));
            B.add_clause_vec(cl);
        }
    }

    B.exactly_tot(oR, k - c.dR);
    B.exactly_tot(oC, k - c.dC);
    if (!skip_global_TS_card) {
        std::vector<int> liveT, liveS;
        for (int i = 0; i < nD; ++i) {
            if (!deadT[i]) liveT.push_back(oT[i]);
            if (!deadS[i]) liveS.push_back(oS[i]);
        }
        B.exactly_tot(liveT, k - c.dT);
        B.exactly_tot(liveS, k - c.dS);
    }

    auto add_zero_waste_direct = [&](int which_set, int color) {
        bool wf = (which_set == 0);
        for (int t = -(n - 1); t <= n - 1; ++t) {
            if ((t & 1) != color || deadT[t + (n - 1)]) continue;
            auto pts = diag_hits(true, t, which_set);
            int lit = litT(t);
            if (wf && pts.size() < 2) { B.add_clause({-lit}); continue; }
            if (!wf && pts.empty()) { B.add_clause({-lit}); continue; }
            for (auto [x, y] : pts) {
                int nl = nonneedy_lit(x, y);
                if (nl != 0) B.add_clause({-lit, -nl});
            }
        }
        for (int s = 0; s <= 2 * n - 2; ++s) {
            if ((s & 1) != color || deadS[s]) continue;
            auto pts = diag_hits(false, s, which_set);
            int lit = litS(s);
            if (wf && pts.size() < 2) { B.add_clause({-lit}); continue; }
            if (!wf && pts.empty()) { B.add_clause({-lit}); continue; }
            for (auto [x, y] : pts) {
                int nl = nonneedy_lit(x, y);
                if (nl != 0) B.add_clause({-lit, -nl});
            }
        }
        auto emit_sq = [&](int x, int y) {
            if (((x + y) & 1) != color) return;
            int t = y - x, s = x + y;
            if (deadT[t + (n - 1)] || deadS[s]) return;
            int nl = nonneedy_lit(x, y);
            if (nl == 0) B.add_clause({-litT(t), -litS(s)});
            else B.add_clause({-litT(t), -litS(s), nl});
        };
        if (which_set == 1 || which_set == 0) {
            for (int y = cc; y <= d; ++y) { emit_sq(a, y); emit_sq(b, y); }
        }
        if (which_set == 2) {
            for (int x = a; x <= b; ++x) { emit_sq(x, cc); emit_sq(x, d); }
        } else if (which_set == 0) {
            for (int x = a + 1; x < b; ++x) { emit_sq(x, cc); emit_sq(x, d); }
        }
    };

    // Deduplicated waste literal builder: when building Bd (which_set==0), skip per-square e/f
    // on squares that are ALREADY forced zero-waste by zero_V or zero_H!
    auto sq_already_zero = [&](int x, int y, int which_set) -> bool {
        if (which_set != 0) return false;
        int col = (x + y) & 1;
        if ((x == a || x == b) && zero_V[col]) return true;
        if ((y == cc || y == d) && zero_H[col]) return true;
        return false;
    };

    auto build_waste_lits = [&](int which_set, int color_filter) {
        bool wf = (which_set == 0);
        std::vector<int> wl;
        for (int t = -(n - 1); t <= n - 1; ++t) {
            if ((color_filter >= 0 && (t & 1) != color_filter) || deadT[t + (n - 1)]) continue;
            auto pts = diag_hits(true, t, which_set);
            int lit = litT(t);
            if (wf) {
                for (int r = 0; r < 2 - static_cast<int>(pts.size()); ++r) wl.push_back(lit);
            } else if (pts.empty()) {
                wl.push_back(lit);
            }
            for (auto [x, y] : pts) {
                if (sq_already_zero(x, y, which_set)) continue;
                int nl = nonneedy_lit(x, y);
                if (nl != 0) {
                    int e = B.new_var();
                    B.add_clause({-lit, -nl, e});
                    wl.push_back(e);
                }
            }
        }
        for (int s = 0; s <= 2 * n - 2; ++s) {
            if ((color_filter >= 0 && (s & 1) != color_filter) || deadS[s]) continue;
            auto pts = diag_hits(false, s, which_set);
            int lit = litS(s);
            if (wf) {
                for (int r = 0; r < 2 - static_cast<int>(pts.size()); ++r) wl.push_back(lit);
            } else if (pts.empty()) {
                wl.push_back(lit);
            }
            for (auto [x, y] : pts) {
                if (sq_already_zero(x, y, which_set)) continue;
                int nl = nonneedy_lit(x, y);
                if (nl != 0) {
                    int e = B.new_var();
                    B.add_clause({-lit, -nl, e});
                    wl.push_back(e);
                }
            }
        }
        auto emit_sq = [&](int x, int y) {
            if (color_filter >= 0 && ((x + y) & 1) != color_filter) return;
            if (sq_already_zero(x, y, which_set)) return;
            int t = y - x, s = x + y;
            if (deadT[t + (n - 1)] || deadS[s]) return;
            int nl = nonneedy_lit(x, y);
            int f = B.new_var();
            if (nl == 0) B.add_clause({-litT(t), -litS(s), f});
            else B.add_clause({-litT(t), -litS(s), nl, f});
            wl.push_back(f);
        };
        if (which_set == 1 || which_set == 0) {
            for (int y = cc; y <= d; ++y) { emit_sq(a, y); emit_sq(b, y); }
        }
        if (which_set == 2) {
            for (int x = a; x <= b; ++x) { emit_sq(x, cc); emit_sq(x, d); }
        } else if (which_set == 0) {
            for (int x = a + 1; x < b; ++x) { emit_sq(x, cc); emit_sq(x, d); }
        }
        return wl;
    };

    for (int col = 0; col < 2; ++col) {
        if (zero_F[col]) add_zero_waste_direct(0, col);
        if (zero_V[col]) add_zero_waste_direct(1, col);
        if (zero_H[col]) add_zero_waste_direct(2, col);
    }

    // Emit remaining non-zero waste bounds
    if (wi.kind == BranchKind::NS_ODD_LONG) {
        int d_long = (w > h) ? c.dC : c.dR;
        if (d_long < 2) B.atmost_tot(build_waste_lits(0, -1), 4 - 2 * d_long);
    } else if (wi.kind == BranchKind::NS_EVEN_ODD) {
        int d_long = (w > h) ? c.dC : c.dR;
        int z_of[2] = {wi.p0, k - wi.p0};
        for (int col = 0; col < 2; ++col) {
            int wf_b = 2 * z_of[col] - k - d_long + 2;
            if (!zero_F[col]) B.atmost_tot(build_waste_lits(0, col), wf_b);
        }
    } else if (wi.kind == BranchKind::NS_EVEN_EVEN) {
        int d_long = (w > h) ? c.dC : c.dR;
        int del = wi.p0;
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        if (!zero_F[q_bar]) B.atmost_tot(build_waste_lits(0, q_bar), 2 * del);
        if (!zero_F[q_star]) B.atmost_tot(build_waste_lits(0, q_star), 4 - 2 * d_long - 2 * del);
    } else if (wi.kind == BranchKind::SQ_DEF2) {
        // Zero-waste everywhere!
    } else if (wi.kind == BranchKind::SQ_DEF1_RC_ODD) {
        int non_zero_side = (c.dR == 1) ? 2 : 1;
        for (int col = 0; col < 2; ++col) {
            B.atmost_tot(build_waste_lits(0, col), 1);
            B.atmost_tot(build_waste_lits(non_zero_side, col), 1);
        }
    } else if (wi.kind == BranchKind::SQ_DEF1_RC_EVEN) {
        int del = wi.p0;
        int non_zero_side = (c.dR == 1) ? 2 : 1;
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        int q_rem = (del == 0) ? q_star : q_bar;
        B.atmost_tot(build_waste_lits(0, q_rem), 2);
        B.atmost_tot(build_waste_lits(non_zero_side, q_rem), 2);
    } else if (wi.kind == BranchKind::SQ_DEF1_TS_ODD) {
        int q_def = wi.p0, q_oth = 1 - q_def;
        int diag_of[2];
        diag_of[u0_t & 1] = u0;
        diag_of[v0_s & 1] = v0;
        int q_rem = (k & 1) ? q_def : q_oth;
        B.atmost_tot(build_waste_lits(0, q_rem), 2);
        auto wlV = build_waste_lits(1, q_rem); wlV.push_back(-diag_of[q_rem]); B.atmost_tot(wlV, 1);
        auto wlH = build_waste_lits(2, q_rem); wlH.push_back(-diag_of[q_rem]); B.atmost_tot(wlH, 1);
    } else if (wi.kind == BranchKind::SQ_DEF1_TS_EVEN) {
        int q_def = wi.p0;
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        if (q_def == q_star) {
            B.atmost_tot(build_waste_lits(0, q_star), 2);
            auto wlV = build_waste_lits(1, q_star); wlV.push_back(-u0); wlV.push_back(-v0); B.atmost_tot(wlV, 1);
            auto wlH = build_waste_lits(2, q_star); wlH.push_back(-u0); wlH.push_back(-v0); B.atmost_tot(wlH, 1);
        } else {
            B.atmost_tot(build_waste_lits(0, q_bar), 2);
            B.atmost_tot(build_waste_lits(1, q_bar), 1);
            B.atmost_tot(build_waste_lits(2, q_bar), 1);
        }
    } else if (wi.kind == BranchKind::SQ0_ODD_W_ODD_K) {
        int q_big = 1 - wi.p0;
        int d_big = ((u0_t & 1) == q_big) ? u0 : v0;
        B.atmost_tot(build_waste_lits(0, q_big), 4);
        auto wlV = build_waste_lits(1, q_big); wlV.push_back(-d_big); B.atmost_tot(wlV, 2);
        auto wlH = build_waste_lits(2, q_big); wlH.push_back(-d_big); B.atmost_tot(wlH, 2);
    } else if (wi.kind == BranchKind::SQ0_ODD_W_EVEN_K) {
        int u0v = wi.p0, v0v = wi.p1;
        int val_col[2];
        val_col[u0_t & 1] = u0v;
        val_col[v0_s & 1] = v0v;
        for (int col = 0; col < 2; ++col) {
            if (val_col[col] == 1) {
                // When d_col == 1, waste_F^(col) == waste_V^(col) + waste_H^(col) identically!
                // So waste_V <= 1 and waste_H <= 1 imply waste_F <= 2 without duplicating border waste vars!
                B.atmost_tot(build_waste_lits(1, col), 1);
                B.atmost_tot(build_waste_lits(2, col), 1);
            } else {
                // When d_col == 0, zero_V & zero_H already hold; keep deduplicated waste_F <= 2!
                B.atmost_tot(build_waste_lits(0, col), 2);
            }
        }
    } else if (wi.kind == BranchKind::SQ0_EVEN_W) {
        int dV = wi.p0, dH = wi.p1, u0v = wi.p2, v0v = wi.p3;
        int q_star = (a + cc) & 1;
        int q_bar = 1 - q_star;
        int uv = u0v + v0v;
        if (!zero_F[q_bar]) B.atmost_tot(build_waste_lits(0, q_bar), 2 * (dV + dH));
        if (!zero_V[q_bar]) B.atmost_tot(build_waste_lits(1, q_bar), 2 * dV);
        if (!zero_H[q_bar]) B.atmost_tot(build_waste_lits(2, q_bar), 2 * dH);
        if (!zero_F[q_star]) B.atmost_tot(build_waste_lits(0, q_star), 4 - 2 * (dV + dH));
        if (!zero_V[q_star]) B.atmost_tot(build_waste_lits(1, q_star), uv - 2 * dV);
        if (!zero_H[q_star]) B.atmost_tot(build_waste_lits(2, q_star), uv - 2 * dH);
    }

    if (use_sbp) {
        B.add_sbp_rc(n, oR, oC, c, allowed_sbp_syms);
    }
    return bf;
}

bool verify_dominating_set(int n, const std::vector<std::pair<int, int>>& D) {
    std::vector<uint8_t> R(n, 0), C(n, 0), T(2 * n, 0), S(2 * n, 0);
    for (auto [x, y] : D) {
        if (x < 0 || x >= n || y < 0 || y >= n) return false;
        R[y] = 1; C[x] = 1; T[y - x + n] = 1; S[x + y] = 1;
    }
    for (int x = 0; x < n; ++x) {
        if (C[x]) continue;
        for (int y = 0; y < n; ++y) {
            if (R[y] || T[y - x + n] || S[x + y]) continue;
            return false;
        }
    }
    return true;
}

// Fast direct posix_spawn of drat-trim (zero /bin/sh overhead!)
bool run_drat_trim(const std::string& drat_trim_bin, const std::string& cnf_path,
                   const std::string& drat_path, double& t_check_s) {
    auto t0 = Clock::now();
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    char* argv_spawn[] = {
        const_cast<char*>(drat_trim_bin.c_str()),
        const_cast<char*>(cnf_path.c_str()),
        const_cast<char*>(drat_path.c_str()),
        const_cast<char*>("-w"),
        const_cast<char*>("-t"),
        const_cast<char*>("1000000"),
        nullptr
    };

    pid_t child = 0;
    int rc = posix_spawn(&child, drat_trim_bin.c_str(), &actions, nullptr, argv_spawn, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        return false;
    }

    std::string out;
    char buf[1024];
    bool read_ok = true;
    for (;;) {
        ssize_t count = read(pipefd[0], buf, sizeof(buf));
        if (count > 0) {
            out.append(buf, static_cast<size_t>(count));
        } else if (count == 0 || errno != EINTR) {
            read_ok = (count == 0);
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    t_check_s = std::chrono::duration<double>(Clock::now() - t0).count();
    if (!read_ok || waited != child || !WIFEXITED(status)) return false;
    std::replace(out.begin(), out.end(), '\r', '\n');
    out.insert(out.begin(), '\n');
    const bool verified = out.find("\ns VERIFIED\n") != std::string::npos;
    // This upstream revision exits 1 after printing "s VERIFIED" on a
    // contradiction found during DIMACS parsing.
    const bool trivial = out.find("c trivial UNSAT\ns VERIFIED\n") != std::string::npos;
    return verified && (WEXITSTATUS(status) == 0 || (WEXITSTATUS(status) == 1 && trivial));
}

} // namespace

int main(int argc, char** argv) {
    int n = 0;
    int jobs = static_cast<int>(std::thread::hardware_concurrency());
    if (jobs < 1) jobs = 4;
    bool certify = false;
    bool use_sbp = true;
    bool use_alg_prune = true;
    bool stop_on_sat = true;
    bool fingerprints = false;
    std::string out_path;
    std::string tags_path;
    std::string proof_dir;
    std::string drat_trim_bin = "./build/drat-trim";
    std::string tmp_dir = "/tmp";
    std::string cfg_mode = "auto";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) n = std::atoi(argv[++i]);
        else if (arg == "--jobs" && i + 1 < argc) jobs = std::atoi(argv[++i]);
        else if (arg == "--certify") certify = true;
        else if (arg == "--no-sbp") use_sbp = false;
        else if (arg == "--no-alg-prune") use_alg_prune = false;
        else if (arg == "--no-stop-on-sat") stop_on_sat = false;
        else if (arg == "--fingerprints") fingerprints = true;
        else if (arg == "--tags" && i + 1 < argc) tags_path = argv[++i];
        else if (arg == "--proof-dir" && i + 1 < argc) proof_dir = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (arg == "--drat-trim" && i + 1 < argc) drat_trim_bin = argv[++i];
        else if (arg == "--tmp" && i + 1 < argc) tmp_dir = argv[++i];
        else if (arg == "--config" && i + 1 < argc) cfg_mode = argv[++i];
        else if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: queen_prover --n <even N> [--jobs J] [--certify] [--out file.jsonl] "
                         "[--drat-trim path] [--tmp dir] [--proof-dir new-dir] [--tags file] "
                         "[--fingerprints] [--no-sbp] [--no-alg-prune] [--config auto|default|unsat]\n";
            return 0;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            return 1;
        }
    }
    if (jobs < 1 || (cfg_mode != "auto" && cfg_mode != "default" && cfg_mode != "unsat")
        || (fingerprints && (certify || !out_path.empty())) || (!certify && !proof_dir.empty())) {
        std::cerr << "Invalid job count, solver configuration, or combination of output options\n";
        return 1;
    }
    if (n < 4 || (n & 1)) {
        std::cerr << "Error: --n must be an even integer >= 4.\n";
        return 1;
    }
    int k = n / 2;

    std::vector<Case> canonical_cases;
    size_t pruned[5] = {0, 0, 0, 0, 0};
    std::vector<WorkItem> work;

    for (int ml = 0; ml <= k; ++ml) {
        for (int mr = 0; mr <= k - ml; ++mr) {
            for (int mb = 0; mb <= k; ++mb) {
                for (int mt = 0; mt <= k - mb; ++mt) {
                    if (n - 1 - ml - mr < 1 || n - 1 - mb - mt < 1) continue;
                    for (int dR = 0; dR <= 2; ++dR) {
                        for (int dC = 0; dC <= 2 - dR; ++dC) {
                            for (int dT = 0; dT <= 2 - dR - dC; ++dT) {
                                for (int dS = 0; dS <= 2 - dR - dC - dT; ++dS) {
                                    Case c{ml, mr, mb, mt, dR, dC, dT, dS};
                                    if (!admissible_even(n, k, c)) continue;
                                    auto imgs = case_images(c);
                                    bool is_min = true;
                                    for (int g = 1; g < 8; ++g) {
                                        if (imgs[g] < c) { is_min = false; break; }
                                    }
                                    if (!is_min) continue;
                                    canonical_cases.push_back(c);
                                    if (use_alg_prune) {
                                        int pr = algebraic_prune(n, k, c);
                                        if (pr > 0) { ++pruned[pr]; continue; }
                                    }
                                    auto items = expand_case(n, k, c);
                                    work.insert(work.end(), items.begin(), items.end());
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::stable_sort(work.begin(), work.end(), [](const WorkItem& a, const WorkItem& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.c < b.c;
    });

    if (!tags_path.empty()) {
        std::ifstream tag_file(tags_path);
        std::vector<std::string> tags;
        for (std::string tag; tag_file >> tag;) tags.push_back(tag);
        std::sort(tags.begin(), tags.end());
        work.erase(std::remove_if(work.begin(), work.end(), [&](const WorkItem& item) {
            return !std::binary_search(tags.begin(), tags.end(), item.tag());
        }), work.end());
        if (tags.empty() || work.size() != tags.size()) {
            std::cerr << "Expected a nonempty file of distinct, generated tags\n";
            return 1;
        }
    }
    if (!proof_dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::create_directory(proof_dir, ec)) {
            std::cerr << "--proof-dir must name a new directory whose parent exists\n";
            return 1;
        }
    }

    std::cerr << "[queen_prover] n=" << n << " k=" << k
              << " | canonical=" << canonical_cases.size()
              << " | pruned(NS1=" << pruned[1] << ",NS2=" << pruned[2]
              << ",NS3=" << pruned[3] << ",SQRC=" << pruned[4] << ")"
              << " | subproblems=" << work.size()
              << " | jobs=" << jobs
              << " | certify=" << (certify ? "ON" : "OFF")
              << " | sbp=" << (use_sbp ? "ON" : "OFF") << std::endl;

    std::ofstream out_fs;
    if (!out_path.empty()) {
        out_fs.open(out_path, std::ios::out | std::ios::trunc);
        if (!out_fs) {
            std::cerr << "Error: cannot open --out " << out_path << "\n";
            return 1;
        }
    }

    auto t_wall0 = Clock::now();
    std::atomic<size_t> next_idx{0};
    std::atomic<size_t> done_cnt{0};
    std::atomic<size_t> sat_cnt{0};
    std::atomic<size_t> unsat_cnt{0};
    std::atomic<size_t> verified_cnt{0};
    std::atomic<size_t> err_cnt{0};
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> total_proof_bytes{0};

    std::mutex io_mtx;
    double solve_elapsed_sum = 0.0;
    double check_elapsed_sum = 0.0;
    double max_solve_s = 0.0;
    std::string max_solve_tag;
    std::vector<std::pair<int, int>> found_witness;

    pid_t pid = getpid();
    size_t total_work = work.size();

    std::signal(SIGTERM, handle_stop_signal);
    std::signal(SIGINT, handle_stop_signal);

    auto worker = [&](int tid) {
        const std::string worker_prefix = tmp_dir + "/qp_" + std::to_string(pid) + "_" + std::to_string(tid);

        while (!stop_flag.load(std::memory_order_relaxed) && !g_signal_stop) {
            size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= total_work) break;
            const WorkItem& wi = work[idx];

            BuiltFormula bf = build_work_item(n, k, wi, use_sbp);
            if (fingerprints) {
                auto digest = bf.cnf.write_dimacs_and_sha256("");
                std::lock_guard<std::mutex> lk(io_mtx);
                std::cout << wi.tag() << '\t' << digest << '\n';
                continue;
            }
            const std::string prefix = proof_dir.empty() ? worker_prefix : proof_dir + "/" + wi.tag();
            const std::string cnf_path = prefix + ".cnf";
            const std::string drat_path = prefix + ".drat";
            std::string sha;
            if (certify) {
                sha = bf.cnf.write_dimacs_and_sha256(cnf_path);
                if (sha.empty()) {
                    ++err_cnt;
                    continue;
                }
            }

            CaDiCaL::Solver solver;
            solver.set("quiet", 1);
            if (cfg_mode == "unsat" || (cfg_mode == "auto" && bf.prefer_unsat_cfg)) {
                solver.configure("unsat");
            }
            if (certify) {
                solver.set("binary", 1);
                if (!solver.trace_proof(drat_path.c_str())) {
                    ++err_cnt;
                    continue;
                }
            }

            for (int l : bf.cnf.lits) solver.add(l);

            auto t0 = Clock::now();
            int res = solver.solve();
            double t_solve = std::chrono::duration<double>(Clock::now() - t0).count();

            if (certify) solver.close_proof_trace();

            bool drat_ok = false;
            double t_check = 0.0;
            uint64_t pbytes = 0;

            if (res == 20) {
                unsat_cnt.fetch_add(1, std::memory_order_relaxed);
                if (certify) {
                    struct stat st {};
                    if (stat(drat_path.c_str(), &st) == 0) pbytes = static_cast<uint64_t>(st.st_size);
                    total_proof_bytes.fetch_add(pbytes, std::memory_order_relaxed);
                    drat_ok = run_drat_trim(drat_trim_bin, cnf_path, drat_path, t_check);
                    if (drat_ok) verified_cnt.fetch_add(1, std::memory_order_relaxed);
                    else err_cnt.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (res == 10) {
                sat_cnt.fetch_add(1, std::memory_order_relaxed);
                std::vector<std::pair<int, int>> D;
                for (auto [v, x, y] : bf.q_vars) {
                    if (solver.val(v) > 0) D.emplace_back(x, y);
                }
                std::sort(D.begin(), D.end());
                bool ok = (static_cast<int>(D.size()) == k) && verify_dominating_set(n, D);
                std::lock_guard<std::mutex> lk(io_mtx);
                if (found_witness.empty()) found_witness = D;
                if (!ok) {
                    std::cerr << "[ERROR] SAT model failed independent domination check on " << wi.tag() << "\n";
                    err_cnt.fetch_add(1, std::memory_order_relaxed);
                }
                if (stop_on_sat) stop_flag.store(true, std::memory_order_relaxed);
            } else {
                err_cnt.fetch_add(1, std::memory_order_relaxed);
            }

            if (certify && proof_dir.empty()) {
                if (res == 20 && drat_ok) {
                    std::remove(cnf_path.c_str());
                    std::remove(drat_path.c_str());
                } else {
                    const std::string keep_prefix = tmp_dir + "/investigate_" + wi.tag();
                    std::rename(cnf_path.c_str(), (keep_prefix + ".cnf").c_str());
                    std::rename(drat_path.c_str(), (keep_prefix + ".drat").c_str());
                }
            }

            size_t d_now = done_cnt.fetch_add(1, std::memory_order_relaxed) + 1;
            {
                std::lock_guard<std::mutex> lk(io_mtx);
                solve_elapsed_sum += t_solve;
                check_elapsed_sum += t_check;
                if (t_solve > max_solve_s) {
                    max_solve_s = t_solve;
                    max_solve_tag = wi.tag();
                }
                if (out_fs.is_open()) {
                    out_fs << "{\"tag\":\"" << wi.tag() << "\""
                           << ",\"case\":[" << wi.c.ml << "," << wi.c.mr << "," << wi.c.mb << "," << wi.c.mt
                           << "," << wi.c.dR << "," << wi.c.dC << "," << wi.c.dT << "," << wi.c.dS << "]"
                           << ",\"kind\":" << static_cast<int>(wi.kind)
                           << ",\"params\":[" << wi.p0 << "," << wi.p1 << "," << wi.p2 << "," << wi.p3 << "," << wi.p4 << "]"
                           << ",\"nv\":" << bf.cnf.nv << ",\"nc\":" << bf.cnf.n_clauses
                           << ",\"res\":\"" << (res == 20 ? "UNSAT" : (res == 10 ? "SAT" : "ERR")) << "\""
                           << ",\"t_solve\":" << std::fixed << std::setprecision(4) << t_solve;
                    if (certify) {
                        out_fs << ",\"cnf_sha256\":\"" << sha << "\""
                               << ",\"proof_bytes\":" << pbytes
                               << ",\"drat\":\"" << (drat_ok ? "VERIFIED" : "FAILED") << "\""
                               << ",\"t_check\":" << std::fixed << std::setprecision(4) << t_check;
                    }
                    out_fs << "}\n" << std::flush;
                }
                if (d_now % 2500 == 0 || d_now == total_work) {
                    double wall = std::chrono::duration<double>(Clock::now() - t_wall0).count();
                    std::cout << "  [progress] " << d_now << "/" << total_work
                              << " (" << std::fixed << std::setprecision(1) << (100.0 * d_now / std::max<size_t>(1, total_work)) << "%)"
                              << " | wall=" << wall << "s | solve_sum=" << solve_elapsed_sum
                              << "s | check_sum=" << check_elapsed_sum
                              << "s | max=" << max_solve_s << "s (" << max_solve_tag << ")"
                              << " | sat=" << sat_cnt.load() << " err=" << err_cnt.load()
                              << std::endl;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < jobs; ++t) threads.emplace_back(worker, t);
    for (auto& th : threads) th.join();
    if (fingerprints) return std::cout ? 0 : 1;

    double wall_s = std::chrono::duration<double>(Clock::now() - t_wall0).count();
    bool complete_unsat = (sat_cnt.load() == 0 && err_cnt.load() == 0 && unsat_cnt.load() == total_work &&
                           (!certify || verified_cnt.load() == total_work));

    std::ostringstream sum;
    sum << "{\"summary\":{\"n\":" << n << ",\"k\":" << k
        << ",\"canonical_cases\":" << canonical_cases.size()
        << ",\"pruned_ns1\":" << pruned[1]
        << ",\"pruned_ns2\":" << pruned[2]
        << ",\"pruned_ns3\":" << pruned[3]
        << ",\"pruned_sqrc\":" << pruned[4]
        << ",\"subproblems\":" << total_work
        << ",\"unsat\":" << unsat_cnt.load()
        << ",\"sat\":" << sat_cnt.load()
        << ",\"verified\":" << verified_cnt.load()
        << ",\"errors\":" << err_cnt.load()
        << ",\"certify\":" << (certify ? "true" : "false")
        << ",\"sbp\":" << (use_sbp ? "true" : "false")
        << ",\"alg_prune\":" << (use_alg_prune ? "true" : "false")
        << ",\"verdict\":\"" << (sat_cnt.load() > 0 ? "SAT" : (complete_unsat ? (certify ? "UNSAT_CERTIFIED" : "UNSAT") : "INCOMPLETE")) << "\""
        << ",\"wall_s\":" << std::fixed << std::setprecision(2) << wall_s
        << ",\"solve_elapsed_sum_s\":" << std::fixed << std::setprecision(2) << solve_elapsed_sum
        << ",\"check_elapsed_sum_s\":" << std::fixed << std::setprecision(2) << check_elapsed_sum
        << ",\"proof_mb\":" << std::fixed << std::setprecision(2) << (total_proof_bytes.load() / 1e6)
        << ",\"max_solve_s\":" << std::fixed << std::setprecision(3) << max_solve_s
        << ",\"max_solve_tag\":\"" << max_solve_tag << "\"";
    if (!found_witness.empty()) {
        sum << ",\"witness\":[";
        for (size_t i = 0; i < found_witness.size(); ++i) {
            if (i) sum << ",";
            sum << "[" << found_witness[i].first << "," << found_witness[i].second << "]";
        }
        sum << "]";
    }
    sum << "}}";

    if (out_fs.is_open()) {
        out_fs << sum.str() << "\n";
        out_fs.close();
        if (!out_fs) return 2;
    }
    std::cout << sum.str() << std::endl;
    return (err_cnt.load() > 0) ? 2 : (sat_cnt.load() > 0 ? 10 : (complete_unsat ? 20 : 1));
}
