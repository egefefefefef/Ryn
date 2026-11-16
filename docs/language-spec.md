# The Ryn Language Specification

**Version 0.1 ("Genesis")**

This document describes the language accepted by the Ryn 0.1 compiler. It is a
description of the implementation, not an aspirational design: constructs that
are not implemented are marked as such and are rejected by the compiler.

Normative grammar: [grammar.ebnf](grammar.ebnf). Tutorial: [syntax.md](syntax.md).

---

## 1. Source text

A Ryn source file is a sequence of UTF-8 bytes named `*.ryn`. The compiler does
not require a byte-order mark; a leading UTF-8 BOM is treated as an error.

Source positions are reported as `file:line:column`, both 1-based. Columns count
bytes, not Unicode scalar values.

### 1.1 Comments

A comment begins with `#` and extends to the end of the line:

```ryn
# This is a comment.
let answer = 42   # and so is this
```

Block comments are planned but **not implemented** in 0.1.

### 1.2 Lines and indentation

Statements are separated by newlines. A line ending in `\n` or `\r\n` is a
statement boundary; a final newline at end of file is optional.

Blocks are expressed with indentation:

- A block is introduced by `:` at the end of a line.
- The block body is indented by one or more spaces relative to the introducing
  line. Any amount of spaces is accepted as long as it is consistent within the
  block.
- **Tabs in indentation are an error** (`E0010`). Use spaces.
- Blank lines and comment-only lines are ignored for indentation purposes.
- Newlines inside `(...)`, `[...]` and `{...}` do not end a statement. This makes
  multi-line calls and array/struct literals possible.

There are no semicolons. A `;` is only accepted inside array types
(`[i32; 4]`); elsewhere it produces `E0009`.

### 1.3 Identifiers

An identifier starts with an ASCII letter or `_` and continues with ASCII
letters, digits or `_`. Identifiers beginning with `_` suppress the unused
variable warning.

Keywords cannot be used as identifiers:

```
let var fn return if else while for in break continue struct enum import
as and or not true false null extern
i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char str void ptr
```

The names `core`, `io`, `math`, `strings` and `mem` are ordinary identifiers,
but `import` gives them a module meaning in the file's top-level scope.

---

## 2. Literals

| Form | Type | Notes |
| --- | --- | --- |
| `0`, `42`, `1_000_000` | untyped integer | Decimal |
| `0xFF`, `0b1010`, `0o755` | untyped integer | Hex, binary, octal; `_` separators allowed |
| `3.14`, `1.0`, `1e-3` | untyped float | A fraction requires a digit after `.`; exponent is optional |
| `true`, `false` | `bool` | |
| `'a'`, `'\n'`, `'\x41'` | `char` | Exactly one byte |
| `"text"` | `str` | UTF-8; escapes below |
| `null` | null pointer | Assignable to any `ptr<T>` |

Integer literals that do not fit in 64 bits are rejected (`E0025`). Untyped
literals take the type demanded by context; without context they default to
`i32` for integers and `f64` for floats.

String escapes: `\n`, `\t`, `\r`, `\0`, `\a`, `\b`, `\f`, `\v`, `\\`, `\'`,
`\"`, `\xNN` (two hex digits), and `\u{XXXX}` (UTF-8 encoded; a `char` can only
hold one byte).

A `str` is an immutable, NUL-terminated byte string. String literals have static
storage duration.

---

## 3. Program structure

A program is a sequence of imports, function declarations, struct declarations
and enum declarations. Imports must appear before any other declaration
(`E0033`).

```ryn
import math
import io

struct Point:
    x: i32
    y: i32

fn main() -> i32:
    return 0
```

Exactly one function named `main` must exist. It must not be `extern`, must take
no parameters, and must return `i32` or `void` (`E0019`, `E0030`). A program
whose `main` returns `void` exits with status 0.

Function bodies may refer to functions declared later in the file; declaration
order does not matter.

---

## 4. Types

### 4.1 Primitive types

| Type | Size | Description |
| --- | --- | --- |
| `i8`, `i16`, `i32`, `i64` | 1, 2, 4, 8 bytes | Signed two's-complement integers |
| `u8`, `u16`, `u32`, `u64` | 1, 2, 4, 8 bytes | Unsigned integers |
| `f32`, `f64` | 4, 8 bytes | IEEE-754 binary32 / binary64 |
| `bool` | 1 byte | `true` or `false` |
| `char` | 1 byte | Unsigned byte, printed as a character |
| `str` | pointer | Immutable NUL-terminated string |
| `void` | — | Only as a function result |

### 4.2 Composite types

```ryn
ptr<T>            # pointer to T
[T; N]            # fixed-size array of N elements
fn(T1, T2) -> R   # function value
Name              # struct or enum
```

Examples:

```ryn
let p: ptr<i32> = &value
let buffer: [u8; 16] = [0; 16]
let callback: fn(i32) -> i32 = square
```

Arrays have a compile-time length and are value types. Function types describe
function values; a function name can be assigned to a function-typed variable,
stored in a struct field, passed as an argument and returned from a function.

Slices (`[]T`) and generics are **not implemented** in 0.1. Their syntax is
recognized and rejected with `E0037`, with a suggestion to use an array or a
pointer.

### 4.3 Structs

```ryn
struct Vector2:
    x: f32
    y: f32
```

A struct is a named product type with named, ordered fields. Fields must have
distinct names (`E0022`). Structs are value types; see the
[memory model](memory-model.md).

Construct a struct with a named-field literal (every field is required):

```ryn
let v = Vector2 { x: 1.0, y: 2.0 }
```

Field access uses `.`. Accessing a field through a `ptr<Struct>` automatically
dereferences the pointer:

```ryn
let ps: ptr<Vector2> = &v
ps.x = 3.0
```

### 4.4 Enums

```ryn
enum Color:
    Red
    Green = 5
    Blue
```

Variants are numbered from 0 unless an explicit integer follows `=`. Explicit
values must be integer literals and may be negative (`Negative = -1`). An enum is represented as `i32`, or `i64` if a
variant does not fit in `i32`. Variants are referenced as `Color.Red`.

Enums support equality and ordering comparisons, printing through a cast to an
integer, and explicit casts to and from integer types.

### 4.5 Type conversions

Implicit conversion is deliberately narrow:

- An untyped integer/float literal adapts to the expected type.
- A signed integer widens to a wider signed integer; an unsigned integer widens
  to a wider unsigned integer. Mixed signedness and narrowing are errors.
- `null` converts to any pointer type.
- A `ptr<T>` converts to `ptr<void>`.
- Arrays and structs do not convert.

Everything else requires an explicit `as` cast (`E0021` if disallowed). Casts
are allowed between numeric types, `bool` and numbers/pointers, `char` and
numbers/pointers, pointers and integers, enums and numbers, and integers to
enums. `str`, arrays and structs cannot be cast.

---

## 5. Declarations

### 5.1 Variables

```ryn
let answer = 42          # immutable, type inferred (i32)
let ratio: f64 = 0.5     # immutable, annotated
var counter: i32 = 0     # mutable
let pending: i32         # declared, uninitialized (reading it is an error)
```

`let` declares an immutable binding; assigning to it is `E0004`. `var` declares
a mutable one. The initializer's type must match the annotation, or be
implicitly convertible to it.

Reading a variable before it is definitely initialized on every path is
`E0029`.

Declarations in the same scope must have distinct names (`E0008`). A declaration
in an inner scope may shadow an outer one; this emits `W0003`.

### 5.2 Functions

```ryn
fn add(a: i32, b: i32) -> i32:
    return a + b

fn greet(name: str):
    println(name)
```

- Parameter types are mandatory.
- The return type is optional; omitting it means `void`.
- A non-`void` function must return a value on every path (`E0012`).
- Parameter names are immutable bindings inside the body.
- Recursion is allowed.
- User functions cannot be overloaded; a duplicate name is `E0008`.
- A call must supply exactly the declared number of arguments (`E0005`) and each
  argument must be assignable to the parameter type (`E0006`).

### 5.3 External functions

```ryn
extern fn puts(s: str) -> i32
```

`extern fn` declares a C function that the generated program links against. The
Ryn type system checks calls. Use `--include <header>` so the generated C file
includes the corresponding declaration:

```sh
ryn build --include stdio.h program.ryn -o program
```

Extern declarations have no body. If a symbol is missing at link time the C
compiler reports it; Ryn does not verify linkage.

### 5.4 Imports

```ryn
import math
```

An import makes a built-in module available under its name (`math.sqrt`,
`math.PI`). Importing an unknown module is `E0026`. Modules are described in
[modules.md](modules.md). Imports do not load files in 0.1.

---

## 6. Statements

### 6.1 Expression statements

A call expression may stand alone as a statement:

```ryn
println("hello")
```

A call whose non-`void` result is discarded is allowed. Other expression
statements whose result is unused (binary or unary expressions) trigger warning
`W0105`.

### 6.2 Assignment

```ryn
x = 5
x += 1
point.x = 2.0
buffer[0] = 9
*p = 42
```

Assignment targets are variables, fields, array/pointer elements and pointer
dereferences. The target must be mutable and assignable (`E0004`, `E0032`).
Compound assignments `+=`, `-=`, `*=`, `/=`, `%=` perform the operation in the
target's type.

### 6.3 Blocks

```ryn
if ready:
    let x = 1
    println(x)
```

A bare indented block is not a statement in 0.1; blocks appear only as bodies of
`if`, `while`, `for` and `fn`.

### 6.4 `if` / `else if` / `else`

```ryn
if x < 0:
    println("negative")
else if x == 0:
    println("zero")
else:
    println("positive")
```

The condition must be `bool`; there is no truthiness (`E0003`). `else if` is a
first-class form; it does not require nested indentation. Both branches may be
omitted only when the construct is used as a statement; an `if` without `else`
does not count as returning a value on all paths.

### 6.5 `while`

```ryn
while not done:
    step()
```

The condition must be `bool`. `break` exits the innermost loop, `continue`
starts the next iteration.

### 6.6 `for`

```ryn
for i in 0..10:      # i = 0, 1, ..., 9
    println(i)

for i in 0..=10:     # i = 0, 1, ..., 10
    println(i)
```

Bounds must be integers. The loop variable is a new immutable binding whose type
is the common integer type of the bounds (untyped bounds become `i32`). The
current implementation evaluates the bounds once before the loop; the end bound
is not re-evaluated. Ranges are only available in `for` headers in 0.1.

### 6.7 `return`

```ryn
return
return value
```

Returning a value from a `void` function, or returning nothing from a non-`void`
function, is a type error. `return` outside a function is `E0017`.

### 6.8 `break` / `continue`

Both require an enclosing loop (`E0016`).

---

## 7. Expressions

### 7.1 Precedence

From highest to lowest:

| Level | Operators | Associativity |
| --- | --- | --- |
| 1 | postfix: call `()`, field `.`, index `[]`, cast `as` | left |
| 2 | unary `-`, `+`, `!`, `~`, `&`, `*` | prefix |
| 3 | `*` `/` `%` | left |
| 4 | `+` `-` | left |
| 5 | `<<` `>>` | left |
| 6 | `&` | left |
| 7 | `^` | left |
| 8 | `\|` | left |
| 9 | `==` `!=` `<` `<=` `>` `>=` | left |
| 10 | `not` | prefix |
| 11 | `and`, `&&` | left |
| 12 | `or`, `\|\|` | left |

Note that `not` binds looser than comparison, so `not a == b` means
`not (a == b)`, and tighter than `and`. The word operators and their symbolic
spellings are interchangeable.

### 7.2 Arithmetic

Arithmetic is defined for integers and floats, but the two operands must have
the same type (after literal adaptation). Mixed types are `E0003`; use `as`.

- Integer `+`, `-`, `*` wrap modulo 2^N (two's complement).
- Integer `/` and `%` truncate toward zero; division by zero panics at run time.
- `INT_MIN / -1` saturates to `INT_MIN`; `INT_MIN % -1` is 0.
- `%` is not defined for floats.
- `bool` and `char` are distinct from integers; use `as` for arithmetic on them.
- Floating-point division by zero follows IEEE-754 (infinity/NaN), not a panic.

### 7.3 Comparisons

`==` and `!=` are defined for numbers, `bool`, `char`, enums, strings and
pointers. `<`, `<=`, `>`, `>=` are defined for numbers, chars, enums and bools,
but **not** for pointers or strings.

Strings compare structurally with `==`/`!=`. Aggregates (structs and arrays) do
not support comparison in 0.1; compare their fields/elements explicitly.

### 7.4 Bitwise and shifts

`&`, `|`, `^`, `~` and shifts require integer operands. A constant shift count
outside `0..63` is a compile-time error (`E0035`). Non-constant shift counts are
masked to `bits - 1` at run time; shifting a 64-bit value by 64 therefore shifts
by 0. `>>` on signed integers is an arithmetic shift (sign-extending); on
unsigned integers it is a logical shift.

### 7.5 Casts

```ryn
value as u8
pointer as ptr<u8>
number as f64
enum_value as i32
```

Casts have no runtime checks; narrowing an integer discards the high bits. A
pointer cast to an integer yields its address.

### 7.6 Calls

```ryn
add(1, 2)
math.sqrt(9.0)
callback(3)
```

A callee must be a function, an extern function, a builtin, or a value of
function type. Calling a non-callable value is `E0020`. There are no default,
named or variadic arguments; `print` and `println` are a fixed family of
one-argument overloads.

### 7.7 Field access and indexing

```ryn
point.x
pointer_to_struct.x
array[3]
pointer[3]
text[0]
```

Indexing a `str` yields a `char` and panics at run time if the index is out of
bounds. Indexing an array with a *constant* index outside its bounds is a
compile-time error (`E0035`); other array indices and all pointer indices are
not checked at run time (see [memory-model.md](memory-model.md)).

### 7.8 Address and dereference

```ryn
let p = &value          # address of a mutable variable, field or element
let q = &point.x
*p = 10
let v = *p
```

`&` requires a mutable lvalue; taking a mutable pointer to an immutable value is
`E0014`. `*` requires a pointer (`E0013`). The pointer type is `ptr<T>` where
`T` is the pointee type.

Pointer arithmetic (`p + 1`) is not supported in 0.1 and is reported as
`E0011`.

---

## 8. Semantic rules

- Scopes are lexical. Blocks introduced by `if`, `while`, `for` and function
  bodies open new scopes.
- Variables must be declared before use (`E0001`), and functions before use is
  not required.
- Definite initialization is tracked across `if`/`else`; a variable assigned in
  only one branch is "maybe initialized" and reading it is `E0029`. Loops are
  conservative: assignments inside a loop do not make a variable initialized
  afterwards unless it was initialized before.
- Unreachable code after `return`, `break` or `continue` triggers `W0102`.
- User functions must return a value on every path; the checker understands
  `if`/`else if`/`else` chains and terminal statements.
- `main` follows the rules in section 3.
- Integer literals are checked against the annotated type (`E0025`).

---

## 9. Standard library

The standard library consists of five built-in modules: `core`, `io`, `math`,
`strings` and `mem`. `core` is always available without an import. See
[modules.md](modules.md) for the complete function and constant list.

Every function listed there has a working C implementation in `runtime/`; none
is a stub. Printing is type-directed: `print`/`println` accept one argument of a
primitive, `str` or pointer type.

---

## 10. Execution model

Ryn compiles a program to a C11 translation unit, then invokes a C compiler.
Generated code:

- wraps `main` and forwards its `i32` result to the process exit status;
- represents `str` as `const char *`, structs as C structs, arrays as C arrays;
- passes and returns structs/arrays by value;
- inserts division-by-zero guards and string bounds checks;
- calls the runtime for printing, input, math and memory management.

A panic prints `panic: <message>` to standard error and terminates the process
with status 1.

---

## 11. Known limitations in 0.1

These are rejected or documented rather than silently mis-compiled:

- slice types (`[]T`) — `E0037`
- generics — `E0037`
- block comments
- pointer arithmetic and pointer ordering
- string ordering (`<` on `str`)
- aggregate (struct/array) equality
- bare array parameters and array return types
- variadic functions and user-defined overloads
- packages or file-based modules; `import` names builtin modules only
- LLVM backend (planned; the IR/backend interface is ready for it)

See [compiler-architecture.md](compiler-architecture.md) for how the stages are
organized and where future backends plug in.
