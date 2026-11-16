# Compiler Architecture

Ryn is a single-pass-per-stage, hand-written compiler. It is written in C17 with
no external dependencies. The pipeline is deliberately conventional so that each
piece can be tested and replaced independently.

```
source file
   │
   ▼
Source Manager ── bytes, line index, SourcePos
   │
   ▼
Lexer ── tokens + INDENT/DEDENT/NEWLINE layout
   │
   ▼
Parser ── AST per source file (arena allocated)
   │
   ▼
Module Loader ── import graph, merged AST, qualified names
   │
   ▼
Semantic Analyzer / Type Checker ── resolves names, types, flow; annotates the AST
   │
   ▼
IR Builder ── typed linear IR with basic blocks
   │
   ▼
C11 Code Generator ── generated .c file + runtime
   │
   ▼
Compiler Driver ── invokes cc, links, produces an executable
```

The driver (`src/compiler.c`) owns the lifetime of every stage and spawns
processes. Source files are read by the source manager on behalf of the module
loader; no other stage touches the filesystem.

## Source layout

| Path | Responsibility |
| --- | --- |
| `include/common.h`, `src/util.c` | Arena allocator, string builder, vectors, filesystem/temp helpers |
| `include/source.h`, `src/source.c` | Source files, line index, `SourcePos` |
| `include/diagnostics.h`, `src/diagnostics.c` | Diagnostic collection, codes, excerpt rendering |
| `include/token.h`, `include/lexer.h`, `src/lexer.c` | Tokens, indentation, literals, comments |
| `include/ast.h`, `src/ast.c` | AST nodes, constructors, debug dump |
| `include/types.h`, `src/types.c` | Type representation, equality, widening, printing |
| `include/parser.h`, `src/parser.c` | Recursive-descent/precedence-climbing parser |
| `include/module.h`, `src/module.c` | Import resolution, multi-file loading, namespace rewriting, AST merge |
| `include/builtins.h`, `src/builtins.c` | Standard library registry and overload tables |
| `include/sema.h`, `src/sema*.c` | Scopes, name resolution, type checking, flow analysis |
| `include/ir.h`, `src/ir.c` | IR data model, lowering, textual dumper |
| `include/codegen.h`, `src/codegen.c` | C11 backend |
| `include/compiler.h`, `src/compiler.c` | Driver: orchestration, temp files, cc invocation |
| `include/package.h`, `src/package.c` | `rynx.toml` model: parsing, validation, writing, lock files |
| `src/main.c` | `ryn` command line parsing and help |
| `src/rynx.c` | `rynx` package manager commands and dependency installation |
| `runtime/` | C runtime linked into generated programs |
| `tests/` | Unit tests and CLI integration tests |

Headers form a DAG: `common → source → diagnostics → token → lexer → ast/types →
parser → module → builtins → sema → ir → codegen → compiler`.

## Stage details

### Source manager

`SourceManager` owns each file's bytes and a line-start table. `SourcePos`
contains a file index, 1-based line/column, and a byte offset. Diagnostics use
the line table to print excerpts.

### Lexer

The lexer produces a flat `TokenVec`. Layout is explicit:

- `NEWLINE` terminates a logical line;
- `INDENT`/`DEDENT` bracket indented blocks;
- newlines are suppressed while `(`, `[` or `{` nesting depth is non-zero;
- comments and blank lines emit nothing;
- tabs in indentation are `E0010`.

Token payloads are decoded eagerly: integer/float values, decoded string bytes,
and char values. The lexer never allocates individual tokens; the vector grows
geometrically and string data lives in the compilation arena.

### Parser

A hand-written recursive-descent parser with precedence climbing for binary
operators. It builds arena-allocated AST nodes and records source positions. On
error it reports once, then synchronizes at the next statement or declaration to
avoid cascades.

The parser is deliberately permissive about later semantic checks (e.g. it
parses `if` conditions as expressions); the semantic analyzer enforces types.

### Module loader

`modules_load_program` starts from the entry file, parses it, then recursively
resolves every `import`. Built-in module names are left for the semantic
analyzer; every other name is looked up next to the importing file, in
`--module-path` and `$RYN_MODULE_PATH` directories, in the manifest's path
dependencies, and in `.rynx/packages`. A canonical-path table detects
duplicate loads and cycles (`E0040`).

Each user module is rewritten before the ASTs are merged. Top-level function,
struct and enum names become `alias.name`; references to the module's own
top-level names are qualified, while qualified references to imported modules
are collapsed into a single identifier. A scope stack keeps local variables and
parameters from being rewritten when they shadow a module member. Import
declarations for user modules disappear from the merged program; built-in
imports are deduplicated. The result is one `AstProgram` for sema, IR and
codegen, so later stages have no notion of files.

### AST and annotations

AST nodes are plain structs with a `kind` and a `SourcePos`. The semantic
analyzer writes its results back into AST slots instead of building a second
tree:

- `AstExpr.type` — resolved expression type;
- `AstIdent.symbol` / `.function` — resolved variable or function;
- `AstCall.function` / `.builtin` / `.indirect` — resolved callee;
- `AstField.field_index` / `.builtin` / `.constant` / `.enum_info`;
- `AstType.resolved` — resolved `Type *`;
- `AstStructDecl.info`, `AstEnumDecl.info`, `AstFnDecl.info`,
  `AstVarDecl.symbol`, `AstFor.symbol`, `AstParam.symbol`.

The IR builder and code generator are therefore consumers of a fully annotated
AST plus IR metadata; they never perform name or type resolution.

### Types

`TypeContext` owns the primitive singletons and interns constructed types
(pointer, array, function). `StructInfo`/`EnumInfo` describe user types and are
created by the analyzer. Types are immutable after construction and compared
with `type_eq`, which is structural.

The analyzer implements:

- lexical scopes and duplicate/shadowing checks;
- untyped literal materialization;
- expected-type propagation into literals, array/struct literals and calls;
- overload resolution for the `print`/`println` family;
- mutability and lvalue rules;
- definite-assignment flow analysis with branch merging;
- return-path analysis (`if`/`else` chains, terminal statements);
- `main` validation;
- warnings for unused variables, unreachable code, shadowing, constant division
  by zero, and discarded results.

### IR

The IR is a typed, linear, three-address representation with explicit basic
blocks. Each function has:

- named **locals** (parameters and source variables), typed;
- typed **temporaries** (SSA-like within a block, but stored as slots);
- ordered **blocks** with instruction lists and explicit terminators
  (`JMP`, `BR`, `RET`).

Key invariants:

- Aggregates (structs and arrays) are handled through **addresses**. A value of
  aggregate type in an expression is a `ptr<Aggregate>` temporary; loads and
  stores of scalars go through `IR_LOAD`/`IR_STORE`.
- Aggregate copies are explicit (`IR_AGG_COPY`), used for initialization,
  assignment, argument passing and returns where value semantics require a copy.
- Calls refer to `FunctionInfo *` or `BuiltinFn *`, never to C name strings. The
  backend resolves `c_name`; keeping names out of the IR lets other backends use
  different mangling.
- Aggregate call results are materialized in a temporary whose address is taken
  with `IR_ADDR_TEMP`.
- String literals are interned per module (`IrString`, addressed by
  `string_id`).
- Every block ends in a terminator; the builder inserts jumps for fall-through
  and a final `RET` for functions.

Instruction set (see `include/ir.h` for exact operands):

```
const.i / const.f / const.str / const.null / func.ptr / copy
bin / cmp / str.eq / str.index / un
call / icall / load / store
addr.local / addr.temp / local.read / local.write
field.addr / elem.addr / cast / agg.copy
jmp / br / ret / nop
```

`ir_dump_module` prints a human-readable listing used by `--emit-ir` and by
tests.

### C11 backend

The backend walks each function once and writes a C11 translation unit:

1. header comment, standard includes, `ryn_runtime.h`, user `--include`s;
2. forward typedefs for structs and enums;
3. typedefs for function types (`typedef R (*ryn_fn_N)(...)`);
4. struct definitions in dependency order, enum typedefs;
5. interned string literals;
6. prototypes (user and extern functions);
7. function bodies: local declarations, temporary declarations, then one C
   label per IR block and one or more statements per instruction;
8. the `main` wrapper.

Type mapping is in [types.md](types.md). Notable details:

- Aggregates are C structs/arrays and are passed and returned by value.
- Function values become typedef'd function pointers so they can be parameters,
  struct fields and return types.
- Integer arithmetic is emitted through the corresponding unsigned type to
  guarantee wrap-around without signed-overflow UB.
- Division/modulo emit a zero check and an `INT_MIN / -1` guard.
- Shifts mask non-constant counts.
- String equality and indexing call runtime helpers.
- Struct field addresses use `&(p->f_x)`; arrays use `[i]`.
- Every non-entry block is reachable by label; the entry has an explicit
  `goto bb0;` to avoid unused-label warnings.

The backend can fail with `E0037` for constructs it cannot spell (for example a
pointer to an array). These are internal limitations, not silently miscompiled
code.

### Runtime

`runtime/` is compiled together with every generated program:

| File | Contents |
| --- | --- |
| `ryn_runtime.h` | Public C API shared by generated code |
| `runtime.c` | Panic/assert, numeric formatting, print family |
| `io.c` | Line-based input, integer/float parsing, flush |
| `math.c` | Wrappers over `<math.h>` |
| `str.c` | Length, equality, concat, from_int, bounds-checked indexing |
| `mem.c` | Checked malloc/realloc/free/copy/zero |

The runtime is plain C17 and has no dependency on the compiler.

### Driver

`compiler_execute` performs:

1. load the input file into the source manager;
2. lex, optionally dump tokens; stop on lexical errors;
3. parse, optionally dump the AST; stop on syntax errors;
4. initialize types and run semantic analysis; stop on errors;
5. build the IR, optionally dump it;
6. for `check`: finish successfully;
7. for `transpile`: write C to `-o` or the derived `.c` name;
8. for `build`/`run`: locate the runtime, create a temporary directory, emit C,
   assemble the C compiler command line, spawn it, and (for `run`) execute the
   produced binary;
9. render diagnostics, remove temporary files, free every stage in reverse
   order.

Runtime discovery order: `$RYN_RUNTIME_DIR`, the compiler's directory
(`runtime`, `../runtime`, `../share/ryn/runtime`), then the compile-time
`RYN_RUNTIME_DIR` definition. This supports both a build tree and an installed
layout (`bin/ryn` + `share/ryn/runtime`).

Process spawning uses `fork`/`execvp` on POSIX and `_spawnvp` on Windows. The
generated C is compiled with `-std=c11`, `-O0 -g` (debug) or `-O2` (release),
with `-I <runtime>` and the five runtime sources plus `-lm`.

## Memory management

The compiler is written to be leak-free:

- Each compilation has one arena for ASTs, types, symbols and IR nodes; it is
  freed wholesale at the end.
- Growable tables (`TokenVec`, diagnostics, IR function/module arrays, function
  pointer registries) are heap allocated and freed explicitly, in reverse order
  of ownership.
- The driver frees its temp paths and removes its temp directory even on error
  paths.
- Unit tests run each compilation through the same teardown path.

Sanitizer builds are recommended when changing ownership:

```sh
cmake -S . -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## Testing strategy

- **Unit tests** compile source strings through lexer/parser/sema/IR/codegen and
  assert on token streams, AST shapes, diagnostic codes and generated C
  fragments (`tests/test_*.c`).
- **CLI tests** drive the real binary (`tests/run_cli_tests.sh`): success and
  failure exit codes, diagnostics text, `check`/`transpile`/`build`/`run`,
  default output names, `--release`, `--include`, argument forwarding, runtime
  panics, and every example program.
- Generated C is independently compiled with a C compiler inside the CLI test,
  proving the backend output is valid C11 rather than merely plausible text.

## Verification status

The 0.2 release has been built and tested with GCC 12 on Linux, including
AddressSanitizer/UndefinedBehaviorSanitizer runs of the compiler and of
generated programs. Clang is supported by the build configuration and the code
uses only standard C17 constructs, but it was not available in the development
sandbox. The Windows-specific driver paths (`_spawnvp`, `GetTempPathA`, `.exe`
suffixes, path handling) are implemented but have not yet been exercised by the
automated tests; they are the first thing to validate on a Windows host.

## Extending the compiler

### Adding a backend

Implement a consumer of `IrModule` analogous to `codegen.c` and select it in the
driver based on `--target`. The IR deliberately contains:

- resolved types,
- explicit blocks and terminators,
- `FunctionInfo`/`BuiltinFn` pointers instead of C names,
- explicit aggregate copies and address operations,
- no C-specific constructs.

An LLVM backend would lower `IR_BIN`/`IR_CMP` to `LLVMBuild*`, blocks to basic
blocks, and aggregates to `byval`/`memcpy` operations. Runtime helpers can be
declared and linked as an object library.

### Adding slices

Slices need:

1. `TY_SLICE` in `include/types.h` and a slice-aware declarator in the parser;
2. a `{ data, length }` representation and conversion from arrays;
3. IR operations for slice creation and checked indexing;
4. runtime helpers for bounds checks;
5. codegen spelling as a small struct.

The IR's address-based aggregate handling and the runtime's checked helpers are
the extension points.

### Adding a language feature

The established order is: lexer token → AST node → parser production → sema
rules → IR lowering → codegen → runtime (if needed) → tests → documentation.
Every stage has a corresponding unit test file; a feature is not complete until
the CLI integration test exercises it end to end.
