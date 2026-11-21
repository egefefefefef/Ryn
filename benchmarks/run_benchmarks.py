#!/usr/bin/env python3
"""Compare Ryn and C on the same workloads.

Every benchmark has two implementations with identical algorithms and output:
a Ryn program and a hand-written C program.  The script compiles both with the
same C compiler and -O2, then reports runtime and build time separately.

Usage:
    python3 benchmarks/run_benchmarks.py [--ryn build/ryn] [--runs 9]

The Ryn binary must be a source build or an installed `ryn` that can find its
runtime directory.  Generated files are kept in a temporary directory.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import resource
except ImportError:  # Windows
    resource = None

BENCHMARKS = {
    "arith": "100M-step 64-bit LCG",
    "fib": "recursive fib(40)",
    "sieve": "sieve of Eratosthenes, 20M",
    "sort": "insertion sort, 50k reversed",
}

# Compiler flags for the C reference.  `ryn build --release` uses -O2 too.
CC_FLAGS = ["-O2", "-std=c11"]
RYN_FLAGS = ["build", "--release"]
BUILD_TIMEOUT = 180
RUN_TIMEOUT = 180


def run_command(cmd: list[str], timeout: int = RUN_TIMEOUT) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=False)


def measure(cmd: list[str], timeout: int = RUN_TIMEOUT) -> dict:
    """Run a command and return wall/CPU time in seconds plus its output."""
    before = resource.getrusage(resource.RUSAGE_CHILDREN) if resource else None
    start = time.perf_counter()
    proc = run_command(cmd, timeout)
    wall = time.perf_counter() - start
    cpu = None
    if before is not None:
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"{proc.stdout}{proc.stderr}"
        )
    return {
        "wall": wall,
        "cpu": cpu,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def resolve_executable(path: str | None, env_name: str, default: str) -> str:
    candidate = path or os.environ.get(env_name) or default
    found = shutil.which(candidate)
    if found:
        return str(Path(found).resolve())
    candidate_path = Path(candidate)
    if candidate_path.is_file():
        return str(candidate_path.resolve())
    raise SystemExit(
        f"error: cannot find executable '{candidate}' "
        f"(use --{env_name.lower()} or ${env_name})"
    )


def pin_prefix(enabled: bool, cpu: int) -> list[str]:
    if not enabled or os.name != "posix" or not shutil.which("taskset"):
        return []
    probe = run_command(["taskset", "-c", str(cpu), "true"])
    if probe.returncode != 0:
        print("note: taskset is not permitted; running without CPU pinning", file=sys.stderr)
        return []
    return ["taskset", "-c", str(cpu)]


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def version_line(cmd: list[str]) -> str:
    try:
        proc = run_command(cmd, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    lines = (proc.stdout + proc.stderr).strip().splitlines()
    return lines[0] if lines else "unknown"


def collect_sources(root: Path) -> list[str]:
    names = sorted(p.stem for p in root.glob("*.ryn"))
    missing = [n for n in names if not (root / f"{n}.c").is_file()]
    if missing:
        raise SystemExit(f"error: missing C implementations for: {', '.join(missing)}")
    return names


def markdown_table(rows: list[list[str]], header: list[str]) -> str:
    out = ["| " + " | ".join(header) + " |", "|" + "|".join(["---"] * len(header)) + "|"]
    for row in rows:
        out.append("| " + " | ".join(row) + " |")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ryn", help="path to the ryn compiler (default: build/ryn or $RYN)")
    parser.add_argument("--cc", help="C compiler used for both languages (default: $CC or cc)")
    parser.add_argument("--runs", type=int, default=9, help="timed runtime runs per binary (default: 9)")
    parser.add_argument("--build-runs", type=int, default=3, help="timed builds per implementation (default: 3)")
    parser.add_argument("--cpu", type=int, default=0, help="CPU to pin to with taskset (default: 0)")
    parser.add_argument("--no-pin", action="store_true", help="do not use taskset")
    parser.add_argument("--json", help="write raw measurements to this JSON file")
    parser.add_argument("--markdown", help="write the report to this Markdown file")
    args = parser.parse_args()

    if args.runs < 1 or args.build_runs < 1:
        raise SystemExit("error: --runs and --build-runs must be positive")

    root = Path(__file__).resolve().parent
    names = collect_sources(root)
    ryn = resolve_executable(args.ryn, "RYN", "build/ryn")
    cc = resolve_executable(args.cc, "CC", "cc")
    pin = pin_prefix(not args.no_pin, args.cpu)
    exe_suffix = ".exe" if os.name == "nt" else ""

    print(f"ryn: {ryn}")
    print(f"cc:  {cc}")
    print(f"pin: {' '.join(pin) if pin else 'none'}")
    print(f"benchmarks: {', '.join(names)}")

    report = {
        "environment": {
            "date": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
            "os": f"{platform.system()} {platform.release()}",
            "machine": platform.machine(),
            "cpu": cpu_model(),
            "cpus": os.cpu_count(),
            "cc": version_line([cc, "--version"]),
            "ryn": version_line([ryn, "--version"]),
            "cc_flags": " ".join(CC_FLAGS),
            "ryn_flags": " ".join(RYN_FLAGS),
            "pinned": bool(pin),
            "runs": args.runs,
            "build_runs": args.build_runs,
        },
        "benchmarks": {},
    }

    with tempfile.TemporaryDirectory(prefix="ryn-bench-") as tmp_name:
        tmp = Path(tmp_name)
        for name in names:
            ryn_src = root / f"{name}.ryn"
            c_src = root / f"{name}.c"
            c_exe = tmp / f"{name}-c{exe_suffix}"
            ryn_exe = tmp / f"{name}-ryn{exe_suffix}"
            gen_c = tmp / f"{name}-ryn.c"
            print(f"\n[{name}] {BENCHMARKS.get(name, '')}")

            c_build_cmd = [cc, *CC_FLAGS, str(c_src), "-o", str(c_exe)]
            ryn_build_cmd = [ryn, *RYN_FLAGS, "--cc", cc, str(ryn_src), "-o", str(ryn_exe)]
            transpile_cmd = [ryn, "transpile", str(ryn_src), "-o", str(gen_c)]

            # Warm the compilers, then time repeated clean builds.
            measure(c_build_cmd, BUILD_TIMEOUT)
            measure(ryn_build_cmd, BUILD_TIMEOUT)
            measure(transpile_cmd, BUILD_TIMEOUT)
            c_builds = [measure(c_build_cmd, BUILD_TIMEOUT)["wall"] for _ in range(args.build_runs)]
            ryn_builds = [measure(ryn_build_cmd, BUILD_TIMEOUT)["wall"] for _ in range(args.build_runs)]
            transpiles = [measure(transpile_cmd, BUILD_TIMEOUT)["wall"] for _ in range(args.build_runs)]

            c_build = statistics.median(c_builds)
            ryn_build = statistics.median(ryn_builds)
            transpile = statistics.median(transpiles)
            gen_size = gen_c.stat().st_size
            with gen_c.open("r", encoding="utf-8", errors="replace") as handle:
                gen_lines = sum(1 for _ in handle)
            c_exe_size = c_exe.stat().st_size
            ryn_exe_size = ryn_exe.stat().st_size

            # Warm both binaries once, then alternate the order to reduce drift.
            c_warm = measure([*pin, str(c_exe)], RUN_TIMEOUT)
            ryn_warm = measure([*pin, str(ryn_exe)], RUN_TIMEOUT)
            if c_warm["stdout"] != ryn_warm["stdout"]:
                raise SystemExit(
                    f"error: {name} output differs\nC:   {c_warm['stdout']!r}\n"
                    f"Ryn: {ryn_warm['stdout']!r}"
                )

            c_samples: list[dict] = []
            ryn_samples: list[dict] = []
            for i in range(args.runs):
                if i % 2 == 0:
                    c_samples.append(measure([*pin, str(c_exe)], RUN_TIMEOUT))
                    ryn_samples.append(measure([*pin, str(ryn_exe)], RUN_TIMEOUT))
                else:
                    ryn_samples.append(measure([*pin, str(ryn_exe)], RUN_TIMEOUT))
                    c_samples.append(measure([*pin, str(c_exe)], RUN_TIMEOUT))

            if any(s["stdout"] != c_samples[0]["stdout"] for s in c_samples + ryn_samples):
                raise SystemExit(f"error: {name} produced inconsistent output")

            def med(samples: list[dict], key: str) -> float:
                return statistics.median(s[key] for s in samples if s[key] is not None)

            c_wall = med(c_samples, "wall") * 1000.0
            ryn_wall = med(ryn_samples, "wall") * 1000.0
            c_cpu = med(c_samples, "cpu") * 1000.0 if resource else None
            ryn_cpu = med(ryn_samples, "cpu") * 1000.0 if resource else None

            entry = {
                "description": BENCHMARKS.get(name, ""),
                "output": c_warm["stdout"].strip(),
                "runtime_ms": {
                    "c_wall_median": c_wall,
                    "ryn_wall_median": ryn_wall,
                    "c_cpu_median": c_cpu,
                    "ryn_cpu_median": ryn_cpu,
                    "ratio_wall": ryn_wall / c_wall,
                    "ratio_cpu": (ryn_cpu / c_cpu) if c_cpu and ryn_cpu else None,
                    "c_wall_samples": [s["wall"] * 1000.0 for s in c_samples],
                    "ryn_wall_samples": [s["wall"] * 1000.0 for s in ryn_samples],
                },
                "build_ms": {
                    "c_cc_median": c_build * 1000.0,
                    "ryn_transpile_median": transpile * 1000.0,
                    "ryn_build_median": ryn_build * 1000.0,
                    "c_build_samples": [x * 1000.0 for x in c_builds],
                    "ryn_build_samples": [x * 1000.0 for x in ryn_builds],
                    "ryn_transpile_samples": [x * 1000.0 for x in transpiles],
                },
                "size_bytes": {
                    "c_source": c_src.stat().st_size,
                    "generated_c": gen_size,
                    "c_binary": c_exe_size,
                    "ryn_binary": ryn_exe_size,
                },
                "generated_c_lines": gen_lines,
            }
            report["benchmarks"][name] = entry
            ratio = entry["runtime_ms"]["ratio_wall"]
            print(
                f"  runtime: C {c_wall:8.1f} ms | Ryn {ryn_wall:8.1f} ms | "
                f"Ryn/C {ratio:5.2f}x | output {entry['output']}"
            )
            print(
                f"  build:   C cc {c_build * 1000:7.1f} ms | "
                f"Ryn transpile {transpile * 1000:6.1f} ms | "
                f"Ryn build {ryn_build * 1000:7.1f} ms"
            )

    ratios = [b["runtime_ms"]["ratio_wall"] for b in report["benchmarks"].values()]
    geo = statistics.geometric_mean(ratios)
    env = report["environment"]

    runtime_rows = []
    build_rows = []
    for name, entry in report["benchmarks"].items():
        rt = entry["runtime_ms"]
        bt = entry["build_ms"]
        runtime_rows.append([
            f"`{name}`",
            entry["description"],
            f"{rt['c_wall_median']:.1f}",
            f"{rt['ryn_wall_median']:.1f}",
            f"{rt['ratio_wall']:.2f}x",
        ])
        build_rows.append([
            f"`{name}`",
            f"{bt['c_cc_median']:.1f}",
            f"{bt['ryn_transpile_median']:.1f}",
            f"{bt['ryn_build_median']:.1f}",
            f"{entry['size_bytes']['generated_c'] / 1024:.1f} KiB",
            f"{entry['size_bytes']['ryn_binary'] / 1024:.1f} KiB",
        ])

    lines = [
        "# C vs Ryn benchmark",
        "",
        f"Generated on {env['date']} on {env['os']} ({env['machine']}), "
        f"{env['cpus']} CPUs, {env['cpu']}.",
        "",
        f"- Ryn: `{env['ryn']}`",
        f"- C compiler: `{env['cc']}`",
        f"- Flags: C `{env['cc_flags']}`; Ryn `{env['ryn_flags']}` "
        "(the Ryn backend uses the same C compiler and `-O2`)",
        f"- Runtime: {env['runs']} timed runs per binary after one warm-up; median wall time, "
        f"CPU pinning: {'yes' if env['pinned'] else 'no'}.",
        f"- Build: {env['build_runs']} timed builds; median wall time.",
        "",
        "## Runtime",
        "",
        markdown_table(
            runtime_rows,
            ["Benchmark", "Workload", "C (ms)", "Ryn (ms)", "Ryn / C"],
        ),
        "",
        f"Geometric mean of Ryn/C runtime ratios: **{geo:.2f}x** "
        f"({'Ryn is slower' if geo > 1 else 'Ryn is faster'}).",
        "",
        "Both implementations use the same algorithm, integer widths and output. "
        "The Ryn executable is generated C compiled with the same `cc -O2`; "
        "the measured difference comes from how the Ryn frontend lowers the program to C.",
        "",
        "## Build time",
        "",
        markdown_table(
            build_rows,
            ["Benchmark", "C `cc -O2` (ms)", "Ryn transpile (ms)", "Ryn build (ms)",
             "Generated C", "Ryn binary"],
        ),
        "",
        "`Ryn transpile` is only lexing/parsing/sema/C generation; `Ryn build` also "
        "compiles the generated C and the runtime with the C compiler.",
        "",
        "## Reproduce",
        "",
        "```sh",
        "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release",
        "cmake --build build",
        "python3 benchmarks/run_benchmarks.py --ryn build/ryn",
        "```",
        "",
        "Raw samples are available with `--json benchmarks/results.json`.",
        "",
    ]
    rendered = "\n".join(lines)
    print("\n" + rendered)
    if args.markdown:
        Path(args.markdown).write_text(rendered, encoding="utf-8")
        print(f"wrote {args.markdown}")
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
