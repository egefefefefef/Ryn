# Diagnostics Reference

Ryn reports problems with stable codes, source locations, a source excerpt with
a caret, and an optional note explaining how to fix the problem.

## Format

```
error[E0003]: type mismatch in return statement: expected 'i32', found 'bool'
   --> examples/broken.ryn:4:12
    |
  4 |     return true
    |            ^^^^
    |
    = explicit conversion is required ('value as T')
```

- Severity is `error` or `warning`.
- Errors use codes `E0001`–`E0099`; warnings use `W0001`–`W0199`.
- The location line points at `file:line:column` (columns are 1-based bytes).
- The excerpt shows the offending line with a gutter and a caret span.
- `= note` lines contain remediation advice.
- Color is used when stderr is a terminal and can be disabled by redirecting.

Errors do not stop at the first problem: the compiler collects up to 100 errors,
reports warnings alongside them, then stops before code generation. Warnings do
not fail a build, and exit status depends only on whether any errors occurred.

At the end of a failed compilation:

```
error: could not compile due to 3 previous errors (1 warning emitted)
```

Warnings never print this summary on their own.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success (possibly with warnings) |
| 1 | Compilation error |
| 2 | Command line usage error |
| 3 | Toolchain or I/O failure (C compiler missing, cannot write files, …) |

`ryn run` replaces these with the program's exit status once compilation and
linking succeed.

## Errors

| Code | Name | Meaning |
| --- | --- | --- |
| E0001 | undefined variable | The name is not in scope, or is used before declaration |
| E0002 | undefined function | No user function or builtin with that name |
| E0003 | type mismatch | Assignment, argument, return, condition or operator type mismatch |
| E0004 | immutable assignment | Assignment to a `let` binding |
| E0005 | argument count | Wrong number of call arguments |
| E0006 | argument type | Builtin overload has no matching parameter types |
| E0007 | unknown type | A type name that is not a primitive, struct or enum |
| E0008 | duplicate | Duplicate declaration in the same scope |
| E0009 | syntax error | Unexpected token, missing punctuation, stray semicolon |
| E0010 | indentation | Tab in indentation, inconsistent dedent |
| E0011 | bad operator | Operator not defined for the given operand types |
| E0012 | missing return | A non-void function can finish without returning |
| E0013 | dereference non-pointer | `*x` where `x` is not a pointer |
| E0014 | invalid address-of | `&` on a non-lvalue or on an immutable binding |
| E0015 | void value | Using a `void` expression as a value |
| E0016 | break outside loop | `break`/`continue` with no enclosing loop |
| E0017 | return outside function | `return` at top level |
| E0018 | unknown field | Field not declared on the struct |
| E0019 | missing main | Program has no `fn main` |
| E0020 | not callable | Calling a non-function value |
| E0021 | bad cast | Unsupported `as` conversion |
| E0022 | duplicate field | Two struct fields with the same name |
| E0023 | not a struct | Field access on a value that is not a struct |
| E0024 | bad index | Indexing a non-array/pointer/string, or non-integer index |
| E0025 | literal range | Literal does not fit the target type |
| E0026 | unknown module | `import` of an unknown module |
| E0027 | not a module | *Reserved* |
| E0028 | reserved name | *Reserved* |
| E0029 | uninitialized | Read of a variable that may be uninitialized |
| E0030 | bad main | `main` has parameters, is extern, or has the wrong result type |
| E0031 | non-constant | *Reserved* |
| E0032 | assign target | Assignment target is not assignable |
| E0033 | import position | `import` after another declaration |
| E0034 | no input | No input file supplied |
| E0035 | out of range | Constant index, shift count, array length or literal out of range |
| E0036 | static init | *Reserved* |
| E0037 | unsupported | Recognized syntax or construct not implemented in 0.1 |
| E0038 | I/O | Cannot read/write a file, cannot run the C compiler |
| E0039 | internal | *Reserved for internal compiler errors* |

Codes marked reserved are defined for forward compatibility and are not
currently emitted.

## Warnings

| Code | Name | Meaning |
| --- | --- | --- |
| W0101 | unused variable | A local variable is never read (prefix with `_` to silence) |
| W0102 | unreachable | Code after `return`/`break`/`continue` |
| W0103 | shadowed | An inner declaration hides an outer one |
| W0104 | division by zero | Constant divisor is zero |
| W0105 | unused result | A binary/unary expression result is discarded |

## Notes and examples

Diagnostics include notes where useful:

```
error[E0004]: cannot assign to immutable variable 'x'
   --> demo.ryn:3:5
    |
  3 |     x = 2
    |     ^
    |
    = declare the variable with 'var' instead of 'let'
```

```
error[E0001]: undefined variable 'y'
   --> demo.ryn:3:13
    |
  3 |     println(y)
    |             ^
    |
    = variables must be declared before they are used
```

Warnings are ordered with errors in source order. A program with warnings still
compiles; the warning text is written to stderr.

## Inspecting stages

When a diagnostic is hard to understand, inspect the compiler stages:

```sh
ryn check --dump-tokens program.ryn
ryn check --dump-ast program.ryn
ryn check --emit-ir program.ryn
ryn transpile program.ryn -o program.c
```

The generated C is ordinary C11 and can be compiled with any C compiler using
the `runtime/` directory.
