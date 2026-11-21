# C vs Ryn benchmark

Four small workloads implemented twice: once in Ryn and once in hand-written C
with the same algorithm, integer widths and output.

| Benchmark | Workload |
| --- | --- |
| `arith` | 100,000,000-step 64-bit LCG (loop-carried multiply/add) |
| `fib` | recursive `fib(40)` (function calls) |
| `sieve` | sieve of Eratosthenes up to 20,000,000 (heap byte array) |
| `sort` | insertion sort of 50,000 reversed `i32` values (heap array) |

## Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 benchmarks/run_benchmarks.py --ryn build/ryn
```

Useful options:

```sh
python3 benchmarks/run_benchmarks.py \
  --runs 11 --build-runs 5 \
  --markdown benchmarks/RESULTS.md \
  --json benchmarks/results.json
```

The runner compiles both sides with the same `cc -O2 -std=c11`, checks that the
outputs are identical, warms each binary once, then times alternating runs and
reports medians. On Linux it pins children to one CPU with `taskset` unless
`--no-pin` is given. Build times are measured separately:

- **C `cc -O2`** — compile the hand-written C source;
- **Ryn transpile** — lex, parse, type-check and generate C;
- **Ryn build** — the full `ryn build --release`, including the C compiler and
  the runtime sources.

`RESULTS.md` and `results.json` contain the last run on the machine described
there. Absolute numbers are hardware-dependent; the Ryn/C ratio is the useful
part.

## Current results

See [RESULTS.md](RESULTS.md). On the reference run (Linux x86-64, GCC 12,
`-O2`), Ryn was within about 0–21% of C at runtime (geometric mean ≈ 1.07x), and
the Ryn frontend itself took 1.6–2.2 ms. The total Ryn build is slower because it
compiles the runtime on every invocation.

## Notes and caveats

- Ryn compiles to C and the generated C is then compiled by the same C compiler,
  so runtime differences come from lowering choices, not from a bytecode VM.
- `[value; N]` with a scalar value is currently expanded into `N` stores in the
  IR. A large fixed-size array literal such as `[0; 20000]` therefore makes the
  generated C huge and the C compiler slow (tens of seconds). The sieve and sort
  benchmarks use `mem.alloc`/`ptr<T>` to avoid measuring that codegen limitation
  in the runtime numbers. Fixing the scalar repeat lowering is a separate task.
- The benchmarks do not cover strings, allocations, module boundaries or I/O;
  they target arithmetic, calls, branches and array access.
- `ryn build` defaults to `-O0`; use `--release` for `-O2`, as this harness does.
