// Ultra-fast Steepest-Descent + Tabu Search Witness Finder for Queen Domination
// Uses O(m) incremental delta evaluation on odd x odd permutation + 1 extra queen (even n, k = n/2 + 1)
// AND general k-queen board search for open odd boards (Q_47, Q_51, Q_59, Q_63, Q_67...).
// Can search for BOTH general dominating sets AND mutually non-attacking (independent) dominating sets!

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

bool verify_dom(int n, const std::vector<std::pair<int, int>>& D, bool& is_indep) {
    if (D.empty()) return false;
    std::vector<int> R(n, 0), C(n, 0), T(2 * n, 0), S(2 * n, 0);
    for (auto [x, y] : D) {
        if (x < 0 || x >= n || y < 0 || y >= n) return false;
        R[y]++; C[x]++; T[y - x + n]++; S[x + y]++;
    }
    is_indep = true;
    for (int v : R) if (v > 1) is_indep = false;
    for (int v : C) if (v > 1) is_indep = false;
    for (int v : T) if (v > 1) is_indep = false;
    for (int v : S) if (v > 1) is_indep = false;
    for (int x = 0; x < n; ++x) {
        if (C[x]) continue;
        for (int y = 0; y < n; ++y) {
            if (R[y] || T[y - x + n] || S[x + y]) continue;
            return false;
        }
    }
    return true;
}

// Fast incremental Tabu Search for even n, k = n/2 + 1:
// m = n/2 queens at (2i+1, 2*p[i]+1) + 1 extra queen at (2*ex, 2*ey) [fixed corner (0,0) or free!].
// Maintains exact uncovered count incrementally in O(m) per diagonal change!
bool find_even_plus1_tabu(int n, bool require_indep, double max_sec, int jobs,
                          std::vector<std::pair<int, int>>& best_D, bool& found_indep) {
    const int m = n / 2;
    const int offT = m;
    const int nT = 2 * m + 1;
    const int nS = 2 * m + 1;

    std::atomic<bool> found{false};
    std::mutex mtx;
    auto t0 = std::chrono::steady_clock::now();

    auto worker = [&](int tid) {
        std::mt19937_64 rng(0x9e3779b97f4a7c15ULL ^ (static_cast<uint64_t>(tid + 1) * 0xbf58476d1ce4e5b9ULL) ^ (n * 1315423911ULL));
        std::vector<int> p(m);
        std::vector<int> cntT(nT, 0), cntS(nS, 0);
        // For each even-even square (u, v) in 0..m-1, track cov = (cntT[v-u+offT]>0) + (cntS[u+v]>0)
        // Even faster: given cntT, cntS, changing a diagonal t from 0->1 or 1->0 touches ONLY the <= m squares on diag t!
        std::vector<int> tabu(m * m, 0);

        // Precompute squares (u, v) on each t and s diagonal!
        std::vector<std::vector<std::pair<int, int>>> sqsT(nT), sqsS(nS);
        for (int u = 0; u < m; ++u) {
            for (int v = 0; v < m; ++v) {
                sqsT[v - u + offT].emplace_back(u, v);
                sqsS[u + v].emplace_back(u, v);
            }
        }

        while (!found.load(std::memory_order_relaxed)) {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > max_sec) break;

            // Pick a corner / edge / interior position for extra queen (ex, ey)
            int ex = (tid % 3 == 0) ? 0 : static_cast<int>(rng() % m);
            int ey = (tid % 3 == 0) ? 0 : static_cast<int>(rng() % m);
            if (require_indep && tid % 3 == 0) { ex = 0; ey = m - 1; } // avoid main diagonal t=0!

            std::iota(p.begin(), p.end(), 0);
            std::shuffle(p.begin(), p.end(), rng);
            std::fill(cntT.begin(), cntT.end(), 0);
            std::fill(cntS.begin(), cntS.end(), 0);
            std::fill(tabu.begin(), tabu.end(), 0);

            for (int i = 0; i < m; ++i) {
                cntT[p[i] - i + offT]++;
                cntS[i + p[i] + 1]++;
            }
            cntT[ey - ex + offT]++;
            cntS[ex + ey]++;

            int uncov = 0, coll = 0;
            for (int u = 0; u < m; ++u) {
                if (u == ex) continue;
                for (int v = 0; v < m; ++v) {
                    if (v == ey) continue;
                    if (cntT[v - u + offT] == 0 && cntS[u + v] == 0) ++uncov;
                }
            }
            for (int i = 0; i < nT; ++i) if (cntT[i] > 1) coll += cntT[i] - 1;
            for (int i = 0; i < nS; ++i) if (cntS[i] > 1) coll += cntS[i] - 1;

            auto addT = [&](int idx, int delta) {
                if (delta == +1) {
                    if (cntT[idx] >= 1) ++coll;
                    if (cntT[idx] == 0) {
                        for (auto [u, v] : sqsT[idx]) {
                            if (u != ex && v != ey && cntS[u + v] == 0) --uncov;
                        }
                    }
                    cntT[idx]++;
                } else {
                    cntT[idx]--;
                    if (cntT[idx] >= 1) --coll;
                    if (cntT[idx] == 0) {
                        for (auto [u, v] : sqsT[idx]) {
                            if (u != ex && v != ey && cntS[u + v] == 0) ++uncov;
                        }
                    }
                }
            };
            auto addS = [&](int idx, int delta) {
                if (delta == +1) {
                    if (cntS[idx] >= 1) ++coll;
                    if (cntS[idx] == 0) {
                        for (auto [u, v] : sqsS[idx]) {
                            if (u != ex && v != ey && cntT[v - u + offT] == 0) --uncov;
                        }
                    }
                    cntS[idx]++;
                } else {
                    cntS[idx]--;
                    if (cntS[idx] >= 1) --coll;
                    if (cntS[idx] == 0) {
                        for (auto [u, v] : sqsS[idx]) {
                            if (u != ex && v != ey && cntT[v - u + offT] == 0) ++uncov;
                        }
                    }
                }
            };

            auto eval_obj = [&]() { return require_indep ? (uncov * 4 + coll * 3) : uncov; };

            const int max_iters = 4000;
            const int tenure_base = std::max(7, m / 3);
            int best_local = eval_obj();
            int stale = 0;

            for (int iter = 1; iter <= max_iters && !found.load(std::memory_order_relaxed); ++iter) {
                if (eval_obj() == 0) {
                    std::vector<std::pair<int, int>> D;
                    D.reserve(m + 1);
                    for (int i = 0; i < m; ++i) D.emplace_back(2 * i + 1, 2 * p[i] + 1);
                    D.emplace_back(2 * ex, 2 * ey);
                    std::sort(D.begin(), D.end());
                    bool ind = false;
                    if (verify_dom(n, D, ind) && (!require_indep || ind)) {
                        std::lock_guard<std::mutex> lk(mtx);
                        if (!found.load()) {
                            best_D = std::move(D);
                            found_indep = ind;
                            found.store(true);
                        }
                        return;
                    }
                }
                if ((iter & 255) == 0) {
                    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > max_sec) break;
                }

                // Evaluate ALL m*(m-1)/2 swaps incrementally!
                int best_val = 1e9;
                int best_i = -1, best_j = -1;
                int ties = 0;

                for (int i = 0; i < m; ++i) {
                    int ti1 = p[i] - i + offT, si1 = i + p[i] + 1;
                    addT(ti1, -1); addS(si1, -1);
                    for (int j = i + 1; j < m; ++j) {
                        int tj1 = p[j] - j + offT, sj1 = j + p[j] + 1;
                        int ti2 = p[j] - i + offT, si2 = i + p[j] + 1;
                        int tj2 = p[i] - j + offT, sj2 = j + p[i] + 1;
                        addT(tj1, -1); addS(sj1, -1);
                        addT(ti2, +1); addS(si2, +1);
                        addT(tj2, +1); addS(sj2, +1);

                        int val = eval_obj();
                        bool is_tabu = (tabu[i * m + j] > iter);
                        if (!is_tabu || val < best_local) {
                            if (val < best_val) {
                                best_val = val; best_i = i; best_j = j; ties = 1;
                            } else if (val == best_val) {
                                ++ties;
                                if (static_cast<int>(rng() % ties) == 0) { best_i = i; best_j = j; }
                            }
                        }

                        addT(tj2, -1); addS(sj2, -1);
                        addT(ti2, -1); addS(si2, -1);
                        addT(tj1, +1); addS(sj1, +1);
                    }
                    addT(ti1, +1); addS(si1, +1);
                }

                if (best_i < 0) break;
                // Apply best swap!
                int i = best_i, j = best_j;
                addT(p[i] - i + offT, -1); addS(i + p[i] + 1, -1);
                addT(p[j] - j + offT, -1); addS(j + p[j] + 1, -1);
                std::swap(p[i], p[j]);
                addT(p[i] - i + offT, +1); addS(i + p[i] + 1, +1);
                addT(p[j] - j + offT, +1); addS(j + p[j] + 1, +1);
                tabu[i * m + j] = iter + tenure_base + static_cast<int>(rng() % 5);

                int cur = eval_obj();
                if (cur < best_local) { best_local = cur; stale = 0; }
                else if (++stale > 180) break; // restart!
            }
        }
    };

    std::vector<std::thread> ths;
    for (int t = 0; t < jobs; ++t) ths.emplace_back(worker, t);
    for (auto& th : ths) th.join();
    return found.load();
}

// Fast incremental Tabu Search for ODD boards Q_n, k = (n+1)/2 (01cover: k even cols x k-1 odd rows, OR 0cover: k even x k even!)
bool find_odd_tabu(int n, const std::string& mode, double max_sec, int jobs,
                   std::vector<std::pair<int, int>>& best_D, int& global_best_uncov) {
    const int k = (n + 1) / 2;
    std::atomic<bool> found{false};
    std::atomic<int> best_u{n * n};
    std::mutex mtx;
    auto t0 = std::chrono::steady_clock::now();

    // For 01cover: queens at (2i, 2*r[i]+1) for i=0..k-1, r[i] in 0..k-2 (all k-1 odd rows covered => one row appears twice!).
    // Uncovered candidates are odd col (2u+1), even row (2v) for u in 0..k-2, v in 0..k-1:
    //   diff diag of queen (2i, 2r+1): 2(r-i)+1 -> t_idx = r - i (in [-(k-1), k-2], offset +k)
    //   sum  diag of queen (2i, 2r+1): 2(i+r)+1 -> s_idx = i + r (in [0, 2k-3])
    //   needy square (2u+1, 2v):
    //     diff diag: 2v - (2u+1) = 2(v-u-1)+1 -> t_idx = v - u - 1!
    //     sum  diag: (2u+1) + 2v = 2(u+v)+1   -> s_idx = u + v!
    // For 0cover: queens at (2i, 2*p[i]) for i=0..k-1 (permutation of 0..k-1).
    //   needy squares (2u+1, 2v+1) for u,v in 0..k-2:
    //     queen diff: 2(p[i]-i) -> t_idx = p[i]-i; needy diff: 2(v-u) -> t_idx = v-u
    //     queen sum:  2(i+p[i]) -> s_idx = i+p[i]; needy sum: 2(u+v+1) -> s_idx = u+v+1
    const bool is_01 = (mode == "01cover");
    const int nU = k - 1;
    const int nV = is_01 ? k : (k - 1);
    const int offT = k;
    const int nT = 2 * k + 2;
    const int nS = 2 * k + 2;

    std::vector<std::vector<std::pair<int, int>>> sqsT(nT), sqsS(nS);
    for (int u = 0; u < nU; ++u) {
        for (int v = 0; v < nV; ++v) {
            int ti = is_01 ? (v - u - 1 + offT) : (v - u + offT);
            int si = is_01 ? (u + v) : (u + v + 1);
            sqsT[ti].emplace_back(u, v);
            sqsS[si].emplace_back(u, v);
        }
    }

    auto worker = [&](int tid) {
        std::mt19937_64 rng(0x517cc1b727220a95ULL ^ (static_cast<uint64_t>(tid + 1) * 0x94d049bb133111ebULL) ^ (n * 2654435761ULL));
        std::vector<int> r(k);
        std::vector<int> cntT(nT, 0), cntS(nS, 0), cntRow(k, 0);
        std::vector<int> tabu(k * k, 0);

        while (!found.load(std::memory_order_relaxed)) {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > max_sec) break;

            if (is_01) {
                r.clear();
                for (int y = 0; y < k - 1; ++y) r.push_back(y);
                r.push_back(static_cast<int>(rng() % (k - 1)));
                std::shuffle(r.begin(), r.end(), rng);
            } else {
                r.resize(k);
                std::iota(r.begin(), r.end(), 0);
                std::shuffle(r.begin(), r.end(), rng);
            }
            std::fill(cntT.begin(), cntT.end(), 0);
            std::fill(cntS.begin(), cntS.end(), 0);
            std::fill(cntRow.begin(), cntRow.end(), 0);
            std::fill(tabu.begin(), tabu.end(), 0);

            for (int i = 0; i < k; ++i) {
                cntRow[r[i]]++;
                cntT[r[i] - i + offT]++;
                cntS[i + r[i]]++;
            }
            int uncov = 0, coll = 0;
            for (int u = 0; u < nU; ++u) {
                for (int v = 0; v < nV; ++v) {
                    int ti = is_01 ? (v - u - 1 + offT) : (v - u + offT);
                    int si = is_01 ? (u + v) : (u + v + 1);
                    if (cntT[ti] == 0 && cntS[si] == 0) ++uncov;
                }
            }
            for (int idx = 0; idx < nT; ++idx) if (cntT[idx] > 1) coll += cntT[idx] - 1;
            for (int idx = 0; idx < nS; ++idx) if (cntS[idx] > 1) coll += cntS[idx] - 1;

            auto addT = [&](int idx, int delta) {
                if (delta == +1) {
                    if (cntT[idx] >= 1) ++coll;
                    if (cntT[idx] == 0) {
                        for (auto [u, v] : sqsT[idx]) {
                            int si = is_01 ? (u + v) : (u + v + 1);
                            if (cntS[si] == 0) --uncov;
                        }
                    }
                    cntT[idx]++;
                } else {
                    cntT[idx]--;
                    if (cntT[idx] >= 1) --coll;
                    if (cntT[idx] == 0) {
                        for (auto [u, v] : sqsT[idx]) {
                            int si = is_01 ? (u + v) : (u + v + 1);
                            if (cntS[si] == 0) ++uncov;
                        }
                    }
                }
            };
            auto addS = [&](int idx, int delta) {
                if (delta == +1) {
                    if (cntS[idx] >= 1) ++coll;
                    if (cntS[idx] == 0) {
                        for (auto [u, v] : sqsS[idx]) {
                            int ti = is_01 ? (v - u - 1 + offT) : (v - u + offT);
                            if (cntT[ti] == 0) --uncov;
                        }
                    }
                    cntS[idx]++;
                } else {
                    cntS[idx]--;
                    if (cntS[idx] >= 1) --coll;
                    if (cntS[idx] == 0) {
                        for (auto [u, v] : sqsS[idx]) {
                            int ti = is_01 ? (v - u - 1 + offT) : (v - u + offT);
                            if (cntT[ti] == 0) ++uncov;
                        }
                    }
                }
            };

            const int max_iters = 8000;
            const int tenure_base = std::max(7, k / 3);
            int best_local = uncov * 16 + coll;
            int stale = 0;

            for (int iter = 1; iter <= max_iters && !found.load(std::memory_order_relaxed); ++iter) {
                if (uncov < best_u.load(std::memory_order_relaxed)) {
                    int prev = best_u.load();
                    while (uncov < prev && !best_u.compare_exchange_weak(prev, uncov)) {}
                }
                if (uncov == 0) {
                    std::vector<std::pair<int, int>> D;
                    D.reserve(k);
                    for (int i = 0; i < k; ++i) {
                        D.emplace_back(2 * i, is_01 ? (2 * r[i] + 1) : (2 * r[i]));
                    }
                    std::sort(D.begin(), D.end());
                    bool ind = false;
                    if (verify_dom(n, D, ind)) {
                        std::lock_guard<std::mutex> lk(mtx);
                        if (!found.load()) { best_D = std::move(D); found.store(true); }
                        return;
                    }
                }
                if ((iter & 255) == 0) {
                    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > max_sec) break;
                }

                int best_val = 1e9;
                int mv_type = 0, mi = -1, mj = -1;
                int ties = 0;

                for (int i = 0; i < k; ++i) {
                    addT(r[i] - i + offT, -1); addS(i + r[i], -1);
                    for (int j = i + 1; j < k; ++j) {
                        if (r[i] == r[j]) continue;
                        addT(r[j] - j + offT, -1); addS(j + r[j], -1);
                        addT(r[j] - i + offT, +1); addS(i + r[j], +1);
                        addT(r[i] - j + offT, +1); addS(j + r[i], +1);

                        int val = uncov * 16 + coll;
                        bool is_tabu = (tabu[i * k + j] > iter);
                        if (!is_tabu || val < best_local) {
                            if (val < best_val) { best_val = val; mv_type = 0; mi = i; mj = j; ties = 1; }
                            else if (val == best_val && static_cast<int>(rng() % (++ties)) == 0) { mv_type = 0; mi = i; mj = j; }
                        }

                        addT(r[i] - j + offT, -1); addS(j + r[i], -1);
                        addT(r[j] - i + offT, -1); addS(i + r[j], -1);
                        addT(r[j] - j + offT, +1); addS(j + r[j], +1);
                    }
                    if (is_01 && cntRow[r[i]] == 2) {
                        for (int ny = 0; ny < k - 1; ++ny) {
                            if (ny == r[i]) continue;
                            addT(ny - i + offT, +1); addS(i + ny, +1);
                            int val = uncov * 16 + coll;
                            bool is_tabu = (tabu[i * k + ny] > iter);
                            if (!is_tabu || val < best_local) {
                                if (val < best_val) { best_val = val; mv_type = 1; mi = i; mj = ny; ties = 1; }
                                else if (val == best_val && static_cast<int>(rng() % (++ties)) == 0) { mv_type = 1; mi = i; mj = ny; }
                            }
                            addT(ny - i + offT, -1); addS(i + ny, -1);
                        }
                    }
                    addT(r[i] - i + offT, +1); addS(i + r[i], +1);
                }

                if (mi < 0) break;
                if (mv_type == 0) {
                    addT(r[mi] - mi + offT, -1); addS(mi + r[mi], -1);
                    addT(r[mj] - mj + offT, -1); addS(mj + r[mj], -1);
                    std::swap(r[mi], r[mj]);
                    addT(r[mi] - mi + offT, +1); addS(mi + r[mi], +1);
                    addT(r[mj] - mj + offT, +1); addS(mj + r[mj], +1);
                    tabu[mi * k + mj] = iter + tenure_base + static_cast<int>(rng() % 5);
                } else {
                    addT(r[mi] - mi + offT, -1); addS(mi + r[mi], -1);
                    cntRow[r[mi]]--;
                    r[mi] = mj;
                    cntRow[r[mi]]++;
                    addT(r[mi] - mi + offT, +1); addS(mi + r[mi], +1);
                    tabu[mi * k + mj] = iter + tenure_base + static_cast<int>(rng() % 5);
                }

                int cur_val = uncov * 16 + coll;
                if (cur_val < best_local) { best_local = cur_val; stale = 0; }
                else if (++stale > 300) break;
            }
        }
    };

    std::vector<std::thread> ths;
    for (int t = 0; t < jobs; ++t) ths.emplace_back(worker, t);
    for (auto& th : ths) th.join();
    global_best_uncov = best_u.load();
    return found.load();
}

} // namespace

int main(int argc, char** argv) {
    std::string mode = "even_plus1";
    int n = 50;
    bool indep = false;
    double timeout = 15.0;
    int jobs = static_cast<int>(std::thread::hardware_concurrency());
    if (jobs < 1) jobs = 8;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--n" && i + 1 < argc) n = std::atoi(argv[++i]);
        else if (a == "--indep") indep = true;
        else if (a == "--timeout" && i + 1 < argc) timeout = std::atof(argv[++i]);
        else if (a == "--jobs" && i + 1 < argc) jobs = std::atoi(argv[++i]);
    }

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::pair<int, int>> D;
    if (mode == "even_plus1") {
        bool is_ind = false;
        bool ok = find_even_plus1_tabu(n, indep, timeout, jobs, D, is_ind);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            std::cout << "{\"n\":" << n << ",\"k\":" << D.size() << ",\"independent\":"
                      << (is_ind ? "true" : "false") << ",\"wall_s\":" << dt << ",\"queens\":[";
            for (size_t i = 0; i < D.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "[" << D[i].first << "," << D[i].second << "]";
            }
            std::cout << "]}" << std::endl;
            return 0;
        }
        std::cout << "{\"n\":" << n << ",\"found\":false,\"wall_s\":" << dt << "}" << std::endl;
        return 1;
    } else {
        int best_u = n * n;
        bool ok = find_odd_tabu(n, mode, timeout, jobs, D, best_u);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            bool is_ind = false;
            verify_dom(n, D, is_ind);
            std::cout << "{\"n\":" << n << ",\"k\":" << D.size() << ",\"mode\":\"" << mode
                      << "\",\"independent\":" << (is_ind ? "true" : "false")
                      << ",\"wall_s\":" << dt << ",\"queens\":[";
            for (size_t i = 0; i < D.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "[" << D[i].first << "," << D[i].second << "]";
            }
            std::cout << "]}" << std::endl;
            return 0;
        }
        std::cout << "{\"n\":" << n << ",\"mode\":\"" << mode << "\",\"found\":false,\"best_uncov\":"
                  << best_u << ",\"wall_s\":" << dt << "}" << std::endl;
        return 1;
    }
}
