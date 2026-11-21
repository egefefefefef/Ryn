# C vs Ryn benchmark

Generated on 2026-11-21 10:10 UTC on Linux 6.1.158+ (x86_64), 2 CPUs, Intel(R) Xeon(R) Processor @ 2.60GHz.

- Ryn: `ryn 0.2.0 (Horizon)`
- C compiler: `x86_64-linux-gnu-gcc-12 (Debian 12.2.0-14+deb12u1) 12.2.0`
- Flags: C `-O2 -std=c11`; Ryn `build --release` (the Ryn backend uses the same C compiler and `-O2`)
- Runtime: 15 timed runs per binary after one warm-up; median wall time, CPU pinning: yes.
- Build: 5 timed builds; median wall time.

## Runtime

| Benchmark | Workload | C (ms) | Ryn (ms) | Ryn / C |
|---|---|---|---|---|
| `arith` | 100M-step 64-bit LCG | 124.6 | 124.7 | 1.00x |
| `fib` | recursive fib(40) | 197.0 | 216.5 | 1.10x |
| `sieve` | sieve of Eratosthenes, 20M | 244.2 | 238.2 | 0.98x |
| `sort` | insertion sort, 50k reversed | 481.0 | 581.6 | 1.21x |

Geometric mean of Ryn/C runtime ratios: **1.07x** (Ryn is slower).

Both implementations use the same algorithm, integer widths and output. The Ryn executable is generated C compiled with the same `cc -O2`; the measured difference comes from how the Ryn frontend lowers the program to C.

## Build time

| Benchmark | C `cc -O2` (ms) | Ryn transpile (ms) | Ryn build (ms) | Generated C | Ryn binary |
|---|---|---|---|---|---|
| `arith` | 41.0 | 2.2 | 302.7 | 1.4 KiB | 28.6 KiB |
| `fib` | 77.3 | 1.6 | 351.1 | 1.3 KiB | 28.6 KiB |
| `sieve` | 73.3 | 1.7 | 355.4 | 4.6 KiB | 28.6 KiB |
| `sort` | 82.6 | 2.0 | 339.4 | 4.5 KiB | 28.6 KiB |

`Ryn transpile` is only lexing/parsing/sema/C generation; `Ryn build` also compiles the generated C and the runtime with the C compiler.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 benchmarks/run_benchmarks.py --ryn build/ryn
```

Raw samples are available with `--json benchmarks/results.json`.
