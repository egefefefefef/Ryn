# Ryn

**Ryn** is a statically typed, indentation-based programming language that compiles
to native executables through C11. The compiler itself is written in C17 and has no
third-party dependencies.

```ryn
fn main() -> i32:
    println("Hello, Ryn!")
    return 0
```

```
$ ryn run hello.ryn
Hello, Ryn!
```

Ryn is designed around a few deliberate choices:

- **Layout is syntax.** Blocks are introduced by `:` and an indented body. There
  are no braces around blocks and no semicolons at the end of statements.
- **Explicit types, real inference.** `let`/`var` infer from the initializer, and
  annotations are available everywhere. There is no implicit narrowing and no
  implicit `int`/`bool` mixing.
- **Values are values.** Structs and arrays are value types: assignment and
  argument passing copy them. Pointers are explicit (`ptr<T>`) and memory is
  managed manually.
- **Small, honest standard library.** Every function in `core`, `io`, `math`,
  `strings` and `mem` is implemented and tested. Nothing is a stub.
- **A readable compilation pipeline.** Source manager → lexer → parser → AST →
  semantic analysis → typed IR → C11 → native executable. Every stage lives in
  its own file and is covered by tests.

## Status

Version **0.1.0 "Genesis"** is the first complete, working implementation:

| Area | State |
| --- | --- |
| Lexer, parser, AST, diagnostics | Working, tested |
| Semantic analysis and type checker | Working, tested |
| Typed linear IR | Working, tested |
| C11 backend and native builds | Working; tested locally with GCC (GCC/Clang jobs are configured in CI) |
| Runtime and standard library | `core`, `io`, `math`, `strings`, `mem` implemented |
| Pointers, structs, enums, arrays, function values | Working |
| Slices and generics | **Not implemented yet** — recognized and rejected with `E0037` |
| Block comments | Not recognized yet (`#` comments only) |
| LLVM backend | Planned; the IR/backend boundary is already in place |

See [docs/language-spec.md](docs/language-spec.md) and the
[roadmap](#roadmap) for details. Unimplemented features are reported as errors;
they are never silently accepted or simulated.

## Building

Requirements:

- A C17 compiler (GCC 11+ or Clang 12+; MSVC is not a supported host compiler yet)
- CMake 3.16 or newer
- A C11 compiler at run time for the generated code (GCC or Clang)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ryn version
```

Plain Makefile-free builds are possible too:

```sh
cc -std=c17 -O2 -Iinclude -DRYN_RUNTIME_DIR="\"$PWD/runtime\"" src/*.c -o ryn
```

## Using the compiler

```
ryn <command> [options] <file.ryn> [-- program args]
```

| Command | What it does |
| --- | --- |
| `ryn check file.ryn` | Type-check without generating code |
| `ryn transpile file.ryn` | Emit the generated C11 (`-o file.c`, default `file.c`) |
| `ryn build file.ryn -o app` | Compile to a native executable |
| `ryn run file.ryn` | Build in a temporary directory and run |
| `ryn version` | Print compiler and language versions |
| `ryn help` | Show usage |

Useful options:

| Option | Meaning |
| --- | --- |
| `--debug` / `--release` | `-O0 -g` (default) or `-O2` for the generated C |
| `--target native` | The only target in 0.1 |
| `--cc <path>` | C compiler to invoke (default `$CC`, then `cc`) |
| `--include <header>` | Add `#include <header>` to the generated C, for `extern` functions |
| `--dump-tokens`, `--dump-ast`, `--emit-ir` | Inspect each compiler stage |
| `-- args` | Reserved for future program arguments; currently accepted and ignored by `run` |

Exit codes: `0` success, `1` compilation error, `2` usage error, `3` toolchain or
I/O failure. `ryn run` exits with the program's own status once the program runs.

The compiler locates the runtime library in this order: `$RYN_RUNTIME_DIR`, a
`runtime` directory next to the compiler executable (or `../runtime`,
`../share/ryn/runtime`), and finally the path recorded at build time.

## A tour

```ryn
import math
import mem

struct Vector2:
    x: f32
    y: f32

fn add(a: Vector2, b: Vector2) -> Vector2:
    return Vector2 { x: a.x + b.x, y: a.y + b.y }

fn main() -> i32:
    let a = Vector2 { x: 1.0, y: 2.0 }
    let b = Vector2 { x: 3.0, y: 4.0 }
    let c = add(a, b)
    println(c.x)                 # 4.0

    var total: i64 = 0
    for i in 0..10:              # 0 .. 9
        total += i as i64
    println(total)               # 45

    let raw: ptr<i32> = mem.alloc(4) as ptr<i32>
    *raw = 7
    println(*raw)                # 7
    mem.free(raw)

    println(math.sqrt(16.0))     # 4.0
    return 0
```

More programs live in [`examples/`](examples):

| File | Shows |
| --- | --- |
| `hello.ryn` | Printing |
| `fibonacci.ryn` | Recursion and `for` ranges |
| `structs.ryn` | Value-type structs and functions |
| `pointers.ryn` | `ptr<T>`, `&`, `*`, `mem.alloc`/`free` |
| `enums.ryn` | Enums, comparisons, casts |
| `io_sum.ryn` | Reading numbers from stdin |

## Project layout

```
include/            public headers for every compiler stage
src/                compiler implementation (C17)
  util.c            arena, strings, vectors, filesystem
  source.c          source manager and positions
  diagnostics.c     diagnostics, codes, source excerpts
  lexer.c           tokens and indentation
  ast.c             AST and debug dumps
  types.c           type system
  parser.c          recursive descent parser
  builtins.c        standard library registry
  sema*.c           scopes, type checking, flow analysis
  ir.c              typed linear IR
  codegen.c         C11 backend
  compiler.c        driver (files, toolchain, processes)
  main.c            command line interface
runtime/            C runtime used by generated programs
tests/              unit tests and CLI integration tests
docs/               specification and architecture documents
examples/           runnable example programs
```

## Testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- `tests/test_lexer.c`, `test_parser.c`, `test_sema.c`, `test_codegen.c` are C unit
  tests linked against the whole compiler pipeline.
- `tests/run_cli_tests.sh` drives the real `ryn` binary end to end: checks,
  diagnostics, builds, transpilation, execution, exit codes, and every example.

Warnings are not failures. Diagnostics are rendered as
`error[E0003]: message`, followed by `file:line:column`, a source excerpt with a
caret, and an optional note. The full code list is in
[docs/diagnostics.md](docs/diagnostics.md).

## Documentation

- [Language specification](docs/language-spec.md)
- [Syntax guide](docs/syntax.md)
- [Formal grammar (EBNF)](docs/grammar.ebnf)
- [Type system](docs/types.md)
- [Memory model](docs/memory-model.md)
- [Modules and standard library](docs/modules.md)
- [Compiler architecture](docs/compiler-architecture.md)
- [Diagnostics reference](docs/diagnostics.md)

## Roadmap

The following are explicitly planned and **not** part of 0.1:

1. Slice types (`[]T`) with bounds-checked views.
2. Generic functions and structs with monomorphisation.
3. Block comments (`#[ ... ]`) and richer string interpolation.
4. Escape analysis and borrow-style lifetime diagnostics for pointers.
5. An LLVM backend reusing the typed IR.
6. Package/module files written in Ryn on top of the existing builtin modules.

## License

Ryn is distributed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
