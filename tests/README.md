# Ryn Tests

The test suite has two layers.

## Unit tests

`ryn_tests` links the whole compiler library (`ryn_core`) and compiles source
strings through the stages:

| File | Covers |
| --- | --- |
| `test_lexer.c` | Token streams, indentation, comments, literals, lexical errors |
| `test_parser.c` | Declarations, precedence, statements, literals, recovery |
| `test_sema.c` | Valid programs, type errors, mutability, flow, main rules, warnings |
| `test_codegen.c` | Generated C fragments for calls, structs, arrays, control flow, division guards, function values |
| `test_package.c` | `rynx.toml` parsing, validation, defaults, round-trips, dependency editing, name rules |

Run them directly:

```sh
./build/ryn_tests
```

or through CTest:

```sh
ctest --test-dir build --output-on-failure
```

`test_util.c` provides `TestCompilation`, which owns one arena, source manager,
diagnostics, lexer, parser, sema, type context and IR module, and frees them in
the correct order. Every test calls `tc_free`, so the suite is leak-clean under
AddressSanitizer.

## CLI integration tests

`run_rynx_tests.sh <rynx-binary> <ryn-binary>` exercises the package manager:
`--version`/`version`, help, `init`, `add`, `install`, `list`, `remove`,
`run`/`build`, lock files, and error paths.

`run_cli_tests.sh <ryn-binary> <runtime-dir> [examples-dir]` exercises the real
compiler binary:

- `version`, `help`, unknown command/option, missing input;
- `check` success and failure, diagnostics codes and carets;
- `--dump-tokens`, `--dump-ast`, `--emit-ir`;
- `run`, exit code forwarding, runtime panics, program arguments;
- `build` with `-o`, `--release`, default output names;
- `transpile`, independent compilation of the generated C, execution of the
  result;
- every program in `examples/`, including stdin-driven `io_sum.ryn`.

The CMake tests `ryn-cli` and `rynx-cli` wire these up automatically on UNIX
systems:

```sh
cmake --build build
ctest --test-dir build -R 'ryn(cli|x-cli)' --output-on-failure
```

## Adding a test

1. Add a `test_*` function to the relevant file and call it from the `run_*_tests`
   entry point.
2. Use `TEST_ASSERT`, `TEST_ASSERT_MSG`, `TEST_ASSERT_INT`, `TEST_ASSERT_STR`,
   and `tc_has_code` for diagnostics.
3. For end-to-end behaviour, add a case to `run_cli_tests.sh` or
   `run_rynx_tests.sh` with `check_status`, `check_output` or `check_contains`.

A feature is complete only when both a unit test and a CLI example exercise it.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

The generated C can be checked the same way by transpiling a program and
compiling the result with `-fsanitize=address,undefined`.
