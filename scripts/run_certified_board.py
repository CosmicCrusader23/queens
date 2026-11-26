#!/usr/bin/env python3
"""Resource-monitored, resumable certified runner for queen_prover.

Features:
- Monitors free disk space, parent+child RSS, swap usage, macOS VM pressure,
  and temporary /tmp CNF/DRAT footprint every 0.5 seconds.
- Gracefully suspends via SIGTERM if free storage drops below --min-free-gb
  (default 8.0 GB) or macOS VM pressure reaches CRITICAL (level >= 4).
- Supports lossless resume from partial verified JSONL records via --tags
  without repeating already-certified UNSAT leaves.
- Never uses --proof-dir; preserves unexpected SAT witnesses or failed proofs.
- Writes complete per-board resource diagnostics JSON and runs independent
  audit + regenerated SHA-256 fingerprint verification immediately upon completion.
"""
import argparse
from datetime import datetime, timezone
from decimal import Decimal
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
import verify_certificate as vc  # noqa: E402


def get_swap_used_mb():
    try:
        out = subprocess.check_output(["sysctl", "-n", "vm.swapusage"], text=True)
        m = re.search(r"used\s*=\s*([0-9.]+)([MG])", out)
        if m:
            val = float(m.group(1))
            return val * 1024.0 if m.group(2) == "G" else val
    except Exception:
        pass
    return 0.0


def get_vm_pressure_level():
    try:
        out = subprocess.check_output(
            ["sysctl", "-n", "kern.memorystatus_vm_pressure_level"], text=True
        ).strip()
        return int(out)
    except Exception:
        return 1


def load_verified_records(part_path: Path):
    """Read all valid verified UNSAT leaf records from an existing partial log."""
    records = {}
    if not part_path.exists():
        return records
    with part_path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                r = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "summary" in r or "tag" not in r:
                continue
            if (
                r.get("res") == "UNSAT"
                and r.get("drat") == "VERIFIED"
                and re.fullmatch(r"[0-9a-f]{64}", r.get("cnf_sha256", ""))
            ):
                records[r["tag"]] = line
    return records


def build_merged_certificate(n: int, part_path: Path, final_path: Path, wall_s: float):
    """Merge verified leaf records across resumed segments and append verified summary."""
    exp_canon, exp_pruned, exp_tags = vc.expected_tags(n)
    records = {}
    raw_lines = []
    with part_path.open() as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            try:
                obj = json.loads(s, parse_float=Decimal)
            except json.JSONDecodeError:
                continue
            if "summary" in obj or "tag" not in obj:
                continue
            tag = obj["tag"]
            if tag in records:
                continue
            assert obj["res"] == "UNSAT" and obj["drat"] == "VERIFIED", tag
            records[tag] = obj
            raw_lines.append(s)

    assert records.keys() == exp_tags, (
        f"Incomplete merge: have {len(records)}, expected {len(exp_tags)}"
    )
    solve_sum = sum(r["t_solve"] for r in records.values())
    check_sum = sum(r["t_check"] for r in records.values())
    total_bytes = sum(r["proof_bytes"] for r in records.values())
    max_r = max(records.values(), key=lambda r: r["t_solve"])

    summary = {
        "summary": {
            "n": n,
            "k": n // 2,
            "canonical_cases": exp_canon,
            "pruned_ns1": exp_pruned[1],
            "pruned_ns2": exp_pruned[2],
            "pruned_ns3": exp_pruned[3],
            "pruned_sqrc": exp_pruned[4],
            "subproblems": len(records),
            "unsat": len(records),
            "sat": 0,
            "verified": len(records),
            "errors": 0,
            "certify": True,
            "sbp": True,
            "alg_prune": True,
            "verdict": "UNSAT_CERTIFIED",
            "wall_s": round(float(wall_s), 2),
            "solve_elapsed_sum_s": round(float(solve_sum), 2),
            "check_elapsed_sum_s": round(float(check_sum), 2),
            "proof_mb": round(total_bytes / 1e6, 2),
            "max_solve_s": round(float(max_r["t_solve"]), 3),
            "max_solve_tag": max_r["tag"],
        }
    }
    tmp_final = final_path.with_suffix(".jsonl.merging")
    with tmp_final.open("w") as out:
        for line in raw_lines:
            out.write(line + "\n")
        out.write(json.dumps(summary, separators=(",", ":")) + "\n")
    tmp_final.replace(final_path)


def run_segment(
    n: int,
    jobs: int,
    prover: Path,
    checker: Path,
    seg_out: Path,
    tmp_dir: Path,
    tags_file: Path | None,
    min_free_gb: float,
    max_pressure: int,
):
    cmd = [
        str(prover),
        "--n", str(n),
        "--jobs", str(jobs),
        "--config", "auto",
        "--certify",
        "--drat-trim", str(checker),
        "--tmp", str(tmp_dir),
        "--out", str(seg_out),
    ]
    if tags_file is not None:
        cmd += ["--tags", str(tags_file)]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    stop_evt = threading.Event()
    diag = {
        "peak_parent_rss_mb": 0.0,
        "peak_children_rss_mb": 0.0,
        "peak_total_rss_mb": 0.0,
        "peak_drat_children": 0,
        "peak_tmp_disk_mb": 0.0,
        "peak_tmp_cnf_mb": 0.0,
        "peak_tmp_drat_mb": 0.0,
        "min_free_disk_gb": round(shutil.disk_usage(tmp_dir).free / (1024**3), 3),
        "peak_swap_used_mb": round(get_swap_used_mb(), 2),
        "max_vm_pressure_level": get_vm_pressure_level(),
        "suspended_reason": None,
        "samples": 0,
    }

    def monitor():
        suspended = False
        suspend_t0 = None
        while not stop_evt.is_set():
            diag["samples"] += 1
            free_gb = shutil.disk_usage(tmp_dir).free / (1024**3)
            if free_gb < diag["min_free_disk_gb"]:
                diag["min_free_disk_gb"] = round(free_gb, 3)

            pressure = get_vm_pressure_level()
            if pressure > diag["max_vm_pressure_level"]:
                diag["max_vm_pressure_level"] = pressure

            if diag["samples"] % 4 == 1:
                sw = get_swap_used_mb()
                if sw > diag["peak_swap_used_mb"]:
                    diag["peak_swap_used_mb"] = round(sw, 2)

            # Sample RSS of prover and all direct children (drat-trim)
            try:
                ps_out = subprocess.check_output(
                    ["ps", "-eo", "pid=,ppid=,rss="], text=True, stderr=subprocess.DEVNULL
                )
                p_kb = c_kb = c_cnt = 0
                for ln in ps_out.splitlines():
                    parts = ln.split()
                    if len(parts) < 3:
                        continue
                    pid, ppid, rss = int(parts[0]), int(parts[1]), int(parts[2])
                    if pid == proc.pid:
                        p_kb = rss
                    elif ppid == proc.pid:
                        c_kb += rss
                        c_cnt += 1
                if p_kb / 1024.0 > diag["peak_parent_rss_mb"]:
                    diag["peak_parent_rss_mb"] = round(p_kb / 1024.0, 2)
                if c_kb / 1024.0 > diag["peak_children_rss_mb"]:
                    diag["peak_children_rss_mb"] = round(c_kb / 1024.0, 2)
                if (p_kb + c_kb) / 1024.0 > diag["peak_total_rss_mb"]:
                    diag["peak_total_rss_mb"] = round((p_kb + c_kb) / 1024.0, 2)
                if c_cnt > diag["peak_drat_children"]:
                    diag["peak_drat_children"] = c_cnt
            except Exception:
                pass

            # Sample temp dir usage
            try:
                tot_b = cnf_b = drat_b = 0
                for entry in os.scandir(tmp_dir):
                    try:
                        sz = entry.stat().st_size
                        tot_b += sz
                        if entry.name.endswith(".cnf"):
                            cnf_b += sz
                        elif entry.name.endswith(".drat"):
                            drat_b += sz
                    except FileNotFoundError:
                        pass
                if tot_b / 1e6 > diag["peak_tmp_disk_mb"]:
                    diag["peak_tmp_disk_mb"] = round(tot_b / 1e6, 2)
                if cnf_b / 1e6 > diag["peak_tmp_cnf_mb"]:
                    diag["peak_tmp_cnf_mb"] = round(cnf_b / 1e6, 2)
                if drat_b / 1e6 > diag["peak_tmp_drat_mb"]:
                    diag["peak_tmp_drat_mb"] = round(drat_b / 1e6, 2)
            except Exception:
                pass

            if not suspended and (free_gb < min_free_gb or pressure >= max_pressure):
                reason = (
                    f"free_disk_gb={free_gb:.2f} < {min_free_gb:.2f}"
                    if free_gb < min_free_gb
                    else f"vm_pressure_level={pressure} >= {max_pressure}"
                )
                diag["suspended_reason"] = reason
                print(f"[GUARD] Suspending n={n} gracefully: {reason}", flush=True)
                try:
                    proc.send_signal(signal.SIGTERM)
                except ProcessLookupError:
                    pass
                suspended = True
                suspend_t0 = time.monotonic()
            elif suspended and free_gb < 4.0 and (time.monotonic() - suspend_t0) > 10.0:
                print(f"[GUARD] Emergency SIGKILL on n={n}: free_disk_gb={free_gb:.2f}", flush=True)
                try:
                    proc.kill()
                except ProcessLookupError:
                    pass
                break

            stop_evt.wait(0.5)

    t_mon = threading.Thread(target=monitor, daemon=True)
    t_mon.start()

    t0 = time.monotonic()
    out_lines = []
    assert proc.stdout is not None
    for ln in proc.stdout:
        out_lines.append(ln.rstrip())
        if "[progress]" in ln or "[queen_prover]" in ln or '"summary"' in ln or "[ERROR]" in ln:
            print(ln, end="", flush=True)
    rc = proc.wait()
    wall_s = time.monotonic() - t0
    stop_evt.set()
    t_mon.join()
    diag["segment_wall_s"] = round(wall_s, 2)
    diag["segment_rc"] = rc
    return rc, wall_s, diag, out_lines


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--jobs", type=int, default=10)
    ap.add_argument("--min-free-gb", type=float, default=8.0)
    ap.add_argument("--max-pressure", type=int, default=4)
    ap.add_argument("--prover", type=Path, default=REPO_DIR / "build/queen_prover")
    ap.add_argument("--checker", type=Path, default=REPO_DIR / "build/drat-trim")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--diag-out", type=Path, default=None)
    args = ap.parse_args()

    n = args.n
    k = n // 2
    out_path = args.out or (REPO_DIR / f"results/cert_n{n}_k{k}.jsonl")
    diag_path = args.diag_out or (REPO_DIR / f"results/diagnostics_n{n}_k{k}.json")
    part_path = out_path.with_suffix(".jsonl.part")

    if out_path.exists():
        print(f"[SKIP] {out_path} already exists; running audit...", flush=True)
        vc.audit_log(n, out_path)
        return 0

    free_gb = shutil.disk_usage("/tmp").free / (1024**3)
    pressure = get_vm_pressure_level()
    if free_gb < args.min_free_gb or pressure >= args.max_pressure:
        print(
            f"[ABORT] Pre-flight guard tripped: free_disk={free_gb:.2f}GB, pressure={pressure}",
            file=sys.stderr,
        )
        return 3

    existing = load_verified_records(part_path)
    tmp_dir = Path(tempfile.mkdtemp(prefix=f"queens_q{n}_"))
    started_utc = datetime.now(timezone.utc).isoformat()
    resumed = bool(existing)

    print(
        f"[START] Q_{n} (k={k}) | jobs={args.jobs} | free_disk={free_gb:.2f}GB | "
        f"pressure={pressure} | swap={get_swap_used_mb():.0f}MB | resumed_leaves={len(existing)} | tmp={tmp_dir}",
        flush=True,
    )

    tags_file = None
    seg_out = part_path
    if resumed:
        _, _, exp_tags = vc.expected_tags(n)
        remaining = sorted(exp_tags - existing.keys())
        print(f"[RESUME] {len(existing)} already verified; {len(remaining)} remaining.", flush=True)
        if remaining:
            tags_file = tmp_dir / f"remaining_n{n}.tags"
            tags_file.write_text("\n".join(remaining) + "\n")
            seg_out = tmp_dir / f"seg_n{n}.jsonl"
        else:
            build_merged_certificate(n, part_path, out_path, 0.0)
            part_path.unlink(missing_ok=True)
            shutil.rmtree(tmp_dir, ignore_errors=True)
            vc.audit_log(n, out_path)
            return 0

    rc, wall_s, diag, out_lines = run_segment(
        n=n,
        jobs=args.jobs,
        prover=args.prover.resolve(),
        checker=args.checker.resolve(),
        seg_out=seg_out,
        tmp_dir=tmp_dir,
        tags_file=tags_file,
        min_free_gb=args.min_free_gb,
        max_pressure=args.max_pressure,
    )

    # Preserve any investigate_* files if failures or SAT occurred
    preserved = []
    for p in tmp_dir.glob("investigate_*"):
        dest = REPO_DIR / "results" / p.name
        shutil.move(str(p), str(dest))
        preserved.append(str(dest))

    # Merge segment into part_path if resumed
    if resumed and seg_out.exists():
        with part_path.open("a") as dst, seg_out.open() as src:
            for ln in src:
                if '"summary"' not in ln:
                    dst.write(ln)

    # Clean temp dir (after preserving any failure artifacts)
    shutil.rmtree(tmp_dir, ignore_errors=True)

    meta = {
        "n": n,
        "k": k,
        "jobs": args.jobs,
        "config": "auto",
        "started_utc": started_utc,
        "finished_utc": datetime.now(timezone.utc).isoformat(),
        "resumed": resumed,
        "rc": rc,
        "preserved_artifacts": preserved,
        "resources": diag,
    }

    if rc == 10:
        meta["status"] = "SAT_FOUND"
        diag_path.write_text(json.dumps(meta, indent=2) + "\n")
        print(f"[SAT] Unexpected SAT on Q_{n}! Summary preserved in {diag_path}", flush=True)
        return 10

    if diag["suspended_reason"] is not None or rc != 20:
        meta["status"] = "SUSPENDED" if diag["suspended_reason"] else f"ERROR_RC_{rc}"
        diag_path.write_text(json.dumps(meta, indent=2) + "\n")
        print(
            f"[HALT] Q_{n} stopped with status={meta['status']} (rc={rc}). "
            f"Partial verified records preserved at {part_path}.",
            flush=True,
        )
        return 3 if diag["suspended_reason"] else (rc or 1)

    # Success! Finalize JSONL
    if resumed:
        prev_wall = 0.0
        if diag_path.exists():
            try:
                prev_wall = json.loads(diag_path.read_text()).get("resources", {}).get("segment_wall_s", 0.0)
            except Exception:
                pass
        build_merged_certificate(n, part_path, out_path, prev_wall + wall_s)
        part_path.unlink(missing_ok=True)
    else:
        part_path.replace(out_path)

    # Full independent audit + regenerated SHA-256 fingerprint match!
    fp_tmp = Path(tempfile.mkdtemp(prefix=f"queens_fp_n{n}_")) / f"n{n}.tsv"
    t_fp0 = time.monotonic()
    with fp_tmp.open("w") as fp_out:
        fp_rc = subprocess.run(
            [str(args.prover.resolve()), "--n", str(n), "--jobs", str(args.jobs), "--fingerprints"],
            stdout=fp_out,
            check=False,
        ).returncode
    assert fp_rc == 0, f"Fingerprint regeneration failed with rc={fp_rc}"
    vc.audit_log(n, out_path, fp_tmp)
    fp_s = time.monotonic() - t_fp0
    shutil.rmtree(fp_tmp.parent, ignore_errors=True)

    # Record summary + diagnostics
    _, summary = vc.read_log(out_path, n)
    meta["status"] = "UNSAT_CERTIFIED_AND_AUDITED"
    meta["fingerprint_audit_s"] = round(fp_s, 2)
    meta["summary"] = {k_: (float(v_) if isinstance(v_, Decimal) else v_) for k_, v_ in summary.items()}
    diag_path.write_text(json.dumps(meta, indent=2) + "\n")
    print(
        f"[DONE] Q_{n} CERTIFIED & AUDITED | wall={meta['summary']['wall_s']}s | "
        f"solve={meta['summary']['solve_elapsed_sum_s']}s | check={meta['summary']['check_elapsed_sum_s']}s | "
        f"proof={meta['summary']['proof_mb']/1000:.2f}GB | peakRSS={diag['peak_total_rss_mb']}MB | "
        f"peakTmp={diag['peak_tmp_disk_mb']}MB | minFreeDisk={diag['min_free_disk_gb']}GB",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
