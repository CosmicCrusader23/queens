#!/usr/bin/env python3
"""Audit the case list, recorded DRAT checks, and explicit witnesses."""
import argparse
from decimal import Decimal
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def case_images(c):
    ml, mr, mb, mt, dR, dC, dT, dS = c
    return [
        (ml, mr, mb, mt, dR, dC, dT, dS),
        (mr, ml, mb, mt, dR, dC, dS, dT),
        (ml, mr, mt, mb, dR, dC, dS, dT),
        (mr, ml, mt, mb, dR, dC, dT, dS),
        (mb, mt, ml, mr, dC, dR, dT, dS),
        (mt, mb, ml, mr, dC, dR, dS, dT),
        (mb, mt, mr, ml, dC, dR, dS, dT),
        (mt, mb, mr, ml, dC, dR, dT, dS),
    ]


def admissible_even(n, k, c):
    ml, mr, mb, mt, dR, dC, dT, dS = c
    if min(dR, dC, dT, dS) < 0 or max(dR, dC, dT, dS) > k - 1:
        return False
    if ml + mr > k - dC or mb + mt > k - dR:
        return False
    w = n - 1 - ml - mr
    h = n - 1 - mb - mt
    if w < 1 or h < 1:
        return False
    if dR + dC + dT + dS > 2:
        return False
    sq = 2 if w == h else 0
    if w >= h and dT + dS + 2 * dR > sq:
        return False
    if h >= w and dT + dS + 2 * dC > sq:
        return False
    return True


def algebraic_prune_independent(n, k, c):
    """Independent verification of algebraic refutation lemmas NS-1, NS-2, NS-3, SQ-RC-Odd."""
    ml, mr, mb, mt, dR, dC, dT, dS = c
    w = n - 1 - ml - mr
    h = n - 1 - mb - mt
    if w == h:
        if (w % 2 == 1) and (k % 2 == 1) and (dR + dC == 1) and (dT == 0 and dS == 0):
            return 4
        return 0
    d_long = dC
    if h > w:
        ml, mr, mb, mt = mb, mt, ml, mr
        w, h = h, w
        d_long = dR
    if (w % 2 == 1) and (k % 2 == 1):
        return 2
    if (w % 2 == 0) and (h % 2 == 1) and (k % 2 == 1) and d_long == 2:
        return 3
    if ml > h // 2 or mr > h // 2:
        return 1
    if ml + mr + mb + mt > k:
        return 1
    for r in range(1, max(ml, mr) + 1):
        if max(0, ml - r + 1) + max(0, mr - r + 1) > h + 1 - 2 * r:
            return 1
    if mb > 0 and (2 * mb > w or w > 2 * h + 2):
        return 1
    if mt > 0 and (2 * mt > w or w > 2 * h + 2):
        return 1
    return 0


def expected_tags(n):
    k = n // 2
    canonical = []
    pruned = {1: 0, 2: 0, 3: 0, 4: 0}
    tags = set()

    def mk(c, kind, p0=0, p1=0, p2=0, p3=0, p4=0):
        return f"{'_'.join(map(str, c))}_b{kind}_{p0}_{p1}_{p2}_{p3}_{p4}"

    for ml in range(k + 1):
        for mr in range(k + 1 - ml):
            for mb in range(k + 1):
                for mt in range(k + 1 - mb):
                    if n - 1 - ml - mr < 1 or n - 1 - mb - mt < 1:
                        continue
                    for dR in range(3):
                        for dC in range(3 - dR):
                            for dT in range(3 - dR - dC):
                                for dS in range(3 - dR - dC - dT):
                                    c = (ml, mr, mb, mt, dR, dC, dT, dS)
                                    if not admissible_even(n, k, c):
                                        continue
                                    imgs = case_images(c)
                                    if c != min(imgs):
                                        continue
                                    canonical.append(c)
                                    pr = algebraic_prune_independent(n, k, c)
                                    if pr > 0:
                                        pruned[pr] += 1
                                        continue
                                    stab = {g for g, im in enumerate(imgs) if im == c}
                                    w = n - 1 - ml - mr
                                    h = n - 1 - mb - mt
                                    sd = dR + dC + dT + dS
                                    if w != h:
                                        ls = w if w > h else h
                                        ss = h if w > h else w
                                        dl = dC if w > h else dR
                                        if ls % 2 == 1:
                                            tags.add(mk(c, 0))
                                        elif ss % 2 == 1:
                                            zlo = (k + dl - 2 + 1) // 2
                                            zhi = (k - dl + 2) // 2
                                            flips = bool(stab & {1, 2, 5, 6})
                                            for z0 in range(zlo, zhi + 1):
                                                if flips and z0 > k - z0:
                                                    continue
                                                tags.add(mk(c, 1, z0))
                                        else:
                                            for dlta in range(0, 2 - dl + 1):
                                                tags.add(mk(c, 2, dlta))
                                    elif sd == 2:
                                        tags.add(mk(c, 3))
                                    elif sd == 1:
                                        if dR == 1 or dC == 1:
                                            if w % 2 == 1:
                                                tags.add(mk(c, 4))
                                            else:
                                                tags.add(mk(c, 5, 0))
                                                tags.add(mk(c, 5, 1))
                                        else:
                                            if w % 2 == 1:
                                                tags.add(mk(c, 6, 0))
                                                tags.add(mk(c, 6, 1))
                                            else:
                                                qs = (ml + mb) & 1
                                                if (ml, mr, mb, mt) == (0, 1, 0, 1):
                                                    tags.add(mk(c, 8, 0))
                                                    tags.add(mk(c, 7, qs, 1))
                                                else:
                                                    tags.add(mk(c, 7, qs, 0))
                                                tags.add(mk(c, 7, 1 - qs, 0))
                                    else:
                                        if w % 2 == 1:
                                            if k % 2 == 1:
                                                tags.add(mk(c, 9, 0))
                                                if not (stab & {1, 2, 5, 6}):
                                                    tags.add(mk(c, 9, 1))
                                            else:
                                                tags.add(mk(c, 10, 1, 1))
                                                tags.add(mk(c, 10, 1, 0))
                                                if not (stab & {1, 2, 5, 6}):
                                                    tags.add(mk(c, 10, 0, 1))
                                                tags.add(mk(c, 10, 0, 0))
                                        else:
                                            if (ml, mr, mb, mt) == (0, 1, 0, 1):
                                                tags.add(mk(c, 12, 0))
                                                tags.add(mk(c, 11, 0, 0, 1, 1, 1))
                                            else:
                                                tags.add(mk(c, 11, 0, 0, 1, 1, 0))
                                            tags.add(mk(c, 11, 1, 0, 1, 1, 0))
                                            if not (stab & {4, 7}):
                                                tags.add(mk(c, 11, 0, 1, 1, 1, 0))
                                            tags.add(mk(c, 11, 1, 1, 1, 1, 0))
                                            tags.add(mk(c, 11, 0, 0, 1, 0, 0))
                                            tags.add(mk(c, 11, 0, 0, 0, 1, 0))
                                            tags.add(mk(c, 11, 0, 0, 0, 0, 0))
    return len(canonical), pruned, tags


def verify_witness(n, queens):
    assert len(queens) == len(set(map(tuple, queens))), "Duplicate queens!"
    R = {y for _, y in queens}
    C = {x for x, _ in queens}
    T = {y - x for x, y in queens}
    S = {x + y for x, y in queens}
    for x, y in queens:
        assert 0 <= x < n and 0 <= y < n, f"Out of bounds {(x, y)}"
    undom = 0
    for x in range(n):
        if x in C:
            continue
        for y in range(n):
            if y in R or (y - x) in T or (x + y) in S:
                continue
            undom += 1
    indep = (len(R) == len(C) == len(T) == len(S) == len(queens))
    return undom == 0, indep


def read_log(path, n):
    records = {}
    summary = None
    with path.open() as f:
        for lineno, line in enumerate(f, 1):
            r = json.loads(line, parse_float=Decimal)
            assert summary is None, f"Record after summary at {path}:{lineno}"
            if "summary" in r:
                summary = r["summary"]
                continue
            tag = r["tag"]
            assert len(r["case"]) == 8 and len(r["params"]) == 5
            case, params = ("_".join(map(str, r[key])) for key in ("case", "params"))
            assert tag == f"{case}_b{r['kind']}_{params}", (path, lineno, tag)
            assert tag not in records, f"Duplicate tag {tag} at {path}:{lineno}"
            assert r["res"] == "UNSAT" and r["drat"] == "VERIFIED", (path, lineno, tag)
            assert re.fullmatch(r"[0-9a-f]{64}", r["cnf_sha256"]), (path, lineno, tag)
            assert all(r[k] >= 0 for k in ("nv", "nc", "t_solve", "t_check", "proof_bytes"))
            records[tag] = r
    assert summary is not None and summary["verdict"] == "UNSAT_CERTIFIED", path
    assert summary["n"] == n and summary["k"] == n // 2, path
    assert summary["sat"] == summary["errors"] == 0 and summary["certify"] is True, path
    assert summary["sbp"] is True and summary["alg_prune"] is True, path
    assert len(records) == summary["subproblems"] == summary["unsat"] == summary["verified"], path
    total_bytes = sum(r["proof_bytes"] for r in records.values())
    assert abs(Decimal(total_bytes) / 10**6 - summary["proof_mb"]) <= Decimal("0.005"), path
    for old_key, new_key, leaf_key in (
        ("solve_cpu_s", "solve_elapsed_sum_s", "t_solve"),
        ("check_cpu_s", "check_elapsed_sum_s", "t_check"),
    ):
        total = summary[new_key] if new_key in summary else summary[old_key]
        rounding = Decimal("0.005") + len(records) * Decimal("0.00005")
        assert abs(sum(r[leaf_key] for r in records.values()) - total) <= rounding, (path, leaf_key)
    return records, summary


def check_retained_proofs(directory, checker, records):
    with tempfile.TemporaryDirectory() as temp:
        for tag, record in sorted(records.items()):
            cnf = directory / f"{tag}.cnf"
            with cnf.open("rb") as f:
                assert hashlib.file_digest(f, "sha256").hexdigest() == record["cnf_sha256"], cnf
            proof = Path(temp) / f"{tag}.drat"
            with gzip.open(directory / f"{tag}.drat.gz", "rb") as source, proof.open("wb") as target:
                shutil.copyfileobj(source, target)
            assert proof.stat().st_size == record["proof_bytes"], proof
            result = subprocess.run(
                [str(checker.resolve()), str(cnf), str(proof), "-w", "-t", "1000000"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
            )
            lines = result.stdout.splitlines()
            trivial = any(line.startswith("c trivial UNSAT") for line in lines)
            assert "s VERIFIED" in lines and (
                result.returncode == 0 or (result.returncode == 1 and trivial)
            ), (tag, result.returncode, result.stdout)
            proof.unlink()
            print(f"  {tag}: retained CNF digest and DRAT proof verified.")


def audit_log(n, certificate, fingerprints=None, replay=None, retained=None, checker=None):
    exp_canon, exp_pruned, exp_tags = expected_tags(n)
    records, summary = read_log(certificate, n)
    missing = exp_tags - records.keys()
    extra = records.keys() - exp_tags
    assert not missing, f"MISSING {len(missing)} expected subproblems! e.g. {sorted(missing)[:5]}"
    assert not extra, f"EXTRA {len(extra)} unexpected subproblems! e.g. {sorted(extra)[:5]}"
    assert summary["canonical_cases"] == exp_canon
    assert [summary[f"pruned_{key}"] for key in ("ns1", "ns2", "ns3", "sqrc")] == list(exp_pruned.values())
    print(f"Q_{n}: {exp_canon} canonical cases, {sum(exp_pruned.values())} pruned, "
          f"{len(records)} leaves with recorded DRAT checks")
    if fingerprints:
        current = {}
        for line in fingerprints.read_text().splitlines():
            tag, digest = line.split("\t")
            assert tag not in current and re.fullmatch(r"[0-9a-f]{64}", digest)
            current[tag] = digest
        assert current.keys() == records.keys(), "Generated and recorded tag sets differ"
        changed = {tag for tag, digest in current.items() if records[tag]["cnf_sha256"] != digest}
        if replay:
            rechecked, replay_summary = read_log(replay, n)
            assert replay_summary["canonical_cases"] == exp_canon
            assert rechecked.keys() == changed, ("Unexpected replay set", sorted(changed ^ rechecked.keys()))
            assert all(row["cnf_sha256"] == current[tag] for tag, row in rechecked.items())
            if retained is not None:
                check_retained_proofs(retained, checker, rechecked)
        else:
            assert not changed, ("CNF fingerprint mismatches", sorted(changed))
        print(f"  {len(current) - len(changed)} archival CNF hashes match, "
              f"{len(changed)} matched to separate replay logs.")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--n", type=int)
    ap.add_argument("--cert", type=Path)
    ap.add_argument("--fingerprints", type=Path)
    ap.add_argument("--replay", type=Path)
    ap.add_argument("--retained", type=Path, help="also check the retained CNF and compressed DRAT pairs")
    ap.add_argument("--checker", type=Path, default=Path("build/drat-trim"))
    ap.add_argument("--witness-json", type=Path)
    args = ap.parse_args()
    if not __debug__:
        ap.error("run with Python assertions enabled")
    if args.cert and (args.n is None or args.n < 4 or args.n % 2):
        ap.error("--cert requires --n to be an even integer at least 4")
    if (args.fingerprints and not args.cert) or (args.replay and not args.fingerprints):
        ap.error("--replay requires --fingerprints, which requires --cert")
    if args.retained and not args.replay:
        ap.error("--retained requires --replay")
    if not args.cert and not args.witness_json:
        ap.error("provide a certificate log or a witness file")

    if args.cert:
        audit_log(args.n, args.cert, args.fingerprints, args.replay, args.retained, args.checker)
    if args.witness_json:
        data = json.loads(args.witness_json.read_text())
        boards = sorted(map(int, data)) if args.n is None else [args.n]
        for n in boards:
            entry = data[str(n)]
            assert entry["n"] == n
            assert len(entry["queens"]) == entry["k"] == n // 2 + 1
            ok, independent = verify_witness(n, entry["queens"])
            assert ok and independent is entry["independent"], n
            print(f"Q_{n}: {entry['k']} queens dominate every square (independent={independent})")


if __name__ == "__main__":
    main()
