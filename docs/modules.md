# Modules and the Standard Library

## Imports

```ryn
import math
```

An import binds a module name in the current file. Members are used with a
qualified name:

```ryn
println(math.sqrt(2.0))
println(math.PI)
```

- Imports must precede all other declarations (`E0033`).
- A module is either one of the five built-in modules below or a user module
  written in Ryn. An import that cannot be resolved is `E0026`.
- Importing the same module twice in one file is `E0008` (duplicate declaration).
- Import cycles are rejected with `E0040`.
- Only top-level names can be imported; there are no nested module paths such as
  `import foo.bar`.

## User modules

A program may be split across files. Importing a name that is not a built-in
module loads another `.ryn` file and makes its top-level functions, structs and
enums available through the import alias:

```ryn
# geometry.ryn
import math

struct Point:
    x: i32
    y: i32

fn length(p: Point) -> f64:
    return math.sqrt((p.x * p.x + p.y * p.y) as f64)
```

```ryn
# main.ryn
import geometry

fn main() -> i32:
    let p = geometry.Point { x: 3, y: 4 }
    println(geometry.length(p))
    return 0
```

Qualified names are used in every position: expressions (`geometry.length`),
types (`geometry.Point`), enum variants (`geometry.Color.Red`) and struct
literals (`geometry.Point { ... }`).

### Resolution order

For `import foo`, the compiler looks for a module file in this order:

1. `foo.ryn` next to the importing file;
2. `foo/mod.ryn` next to the importing file;
3. a `path` dependency named `foo` in the `rynx.toml` closest to the importing
   file;
4. `<name>.ryn` and `<name>/mod.ryn` in each `--module-path` directory, in the
   order the flags were given;
5. the same layouts in directories listed in `$RYN_MODULE_PATH` (split by `:`
   on Unix and `;` on Windows);
6. `.rynx/packages/foo`, found by walking up from the entry file to the project
   root.

Built-in modules always win: a local `math.ryn` cannot shadow `import math`.
A source file may only be imported under one alias per build; importing the same
file as both `foo` and `bar` is `E0008`. Different files may use the same alias
in different modules, because each import is qualified internally.

A package directory may contain the module in any of these locations. The first
one that exists is used:

```
src/lib.ryn
src/<package>.ryn
src/mod.ryn
<package>.ryn
lib.ryn
mod.ryn
entry = "..." from rynx.toml
```

`rynx build` and `rynx run` pass the project's `.rynx/packages` directory to the
compiler automatically. `path` dependencies are resolved through the manifest of
the importing package, so they can be imported without copying.

### Visibility and name conflicts

Every top-level declaration in a module is public in Ryn 0.2. Declarations keep
their names inside the module but are namespaced by the import alias across
modules, so two packages may both define `Point` or `length`. Within a module,
members of the same module are used without a prefix; members of another module
always require the alias.

Local variables and parameters shadow top-level names, including import aliases.
A local declaration is never rewritten to a module member.

All modules are compiled into one program. Only the entry file may define `main`;
a `main` in an imported module is renamed like any other declaration.

### Example package layout

```
demo/
  rynx.toml
  src/main.ryn          import utils
  .rynx/packages/utils/
    rynx.toml           entry = "src/lib.ryn"
    src/lib.ryn
```

```ryn
# src/main.ryn
import utils

fn main() -> i32:
    println(utils.greeting())
    return 0
```

---

## Built-in modules

The `core` module is the same table as the global namespace, so the following
are equivalent:

```ryn
println(len("abc"))
```

```ryn
import core
println(core.len("abc"))
```

---

## `core` — always available

| Function | Signature | Behaviour |
| --- | --- | --- |
| `print` | `print(x)` for `i8`…`i64`, `u8`…`u64`, `f32`, `f64`, `bool`, `char`, `str`, `ptr<T>` | Writes one value without a newline |
| `println` | `println(x)`, same types | Writes one value followed by a newline |
| `panic` | `panic(message: str) -> void` | Prints `panic: message` to stderr and exits 1 |
| `assert` | `assert(condition: bool) -> void` | Panics if the condition is false |
| `assert_msg` | `assert_msg(condition: bool, message: str) -> void` | Panics with `message` if false |
| `len` | `len(s: str) -> u64` | Byte length of a string |

Printing details:

- integers print in decimal, no separators;
- floats always contain a decimal point or exponent (`1.0`, `1e+20`);
  infinities print `inf`/`-inf`, NaN prints `nan`;
- `bool` prints `true`/`false`;
- `char` prints the raw byte;
- pointers print their address, or `null`;
- strings print their contents, or `(null)` for a null `str`.

---

## `io` — console input and output

```ryn
import io
```

| Function | Signature | Behaviour |
| --- | --- | --- |
| `read_line` | `read_line() -> str` | Reads one line (without `\n`) from stdin. At end of input returns an empty string. The returned buffer is owned by the caller and can be freed with `strings.free`. |
| `read_int` | `read_int() -> i32` | Reads one line and parses a complete decimal integer. Panics on bad input, overflow, or end of input. |
| `read_float` | `read_float() -> f64` | Reads one line and parses a complete float. Panics on bad input. |
| `flush` | `flush() -> void` | Flushes stdout (useful before prompting) |

Input is **line based**: `read_int` consumes an entire line and requires it to
contain exactly one integer. Two numbers on one line are rejected; call
`read_int` once per line, or use `read_line` and split it yourself.

`io.print` and `io.println` are also available and behave like the globals.

There is no file I/O; only standard input/output are exposed.

---

## `math` — functions and constants

All trigonometric/logarithmic functions take and return `f64`. Integer overloads
exist for `min`, `max` and `abs`.

| Function | Signature |
| --- | --- |
| `sqrt`, `cbrt`, `exp` | `(f64) -> f64` |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan` | `(f64) -> f64` |
| `ln`, `log2`, `log10` | `(f64) -> f64` |
| `floor`, `ceil`, `round`, `trunc`, `fabs` | `(f64) -> f64` |
| `pow`, `atan2`, `fmod`, `hypot` | `(f64, f64) -> f64` |
| `min`, `max` | `(f64, f64) -> f64`, `(i32, i32) -> i32`, `(i64, i64) -> i64` |
| `abs` | `(i32) -> i32`, `(i64) -> i64` |

Constants:

| Constant | Meaning |
| --- | --- |
| `math.PI` | π |
| `math.E` | e |
| `math.TAU` | 2π |
| `math.INF` | Positive infinity |
| `math.NAN` | Not-a-number |

`abs(INT32_MIN)` and `abs(INT64_MIN)` return the minimum value unchanged, because
its absolute value does not fit in the signed type.

---

## `strings` — string helpers

```ryn
import strings
```

| Function | Signature | Behaviour |
| --- | --- | --- |
| `len` | `len(s: str) -> u64` | Byte length |
| `eq` | `eq(a: str, b: str) -> bool` | Content equality (same as `==`) |
| `concat` | `concat(a: str, b: str) -> str` | Allocates a new string; caller owns it |
| `from_int` | `from_int(value: i64) -> str` | Allocates the decimal representation; caller owns it |
| `free` | `free(s: str) -> void` | Frees an owned string; do not pass a literal |

`str` values are immutable. Indexing (`s[i]`) is bounds-checked and yields a
`char`. Strings compare with `==`/`!=`; they cannot be ordered.

The language does not track string ownership. It is your responsibility to call
`strings.free` exactly once on each heap-allocated string and never on literals.

---

## `mem` — raw memory

```ryn
import mem
```

| Function | Signature | Behaviour |
| --- | --- | --- |
| `alloc` | `alloc(size: u64) -> ptr<void>` | Allocates `size` bytes (0 means 1). Panics on failure. Uninitialized. |
| `realloc` | `realloc(p: ptr<void>, size: u64) -> ptr<void>` | Resizes an allocation; panics on failure |
| `free` | `free(p: ptr<void>) -> void` | Releases memory; `free(null)` is a no-op |
| `copy` | `copy(dst: ptr<void>, src: ptr<void>, size: u64) -> void` | `memmove` semantics |
| `zero` | `zero(p: ptr<void>, size: u64) -> void` | Fills with zero bytes |

Any pointer converts implicitly to `ptr<void>`, so `mem.free(any_pointer)` works.
See [memory-model.md](memory-model.md) for the full ownership rules.

---

## Complete example

```ryn
import math
import io
import strings
import mem

fn main() -> i32:
    print("Radius: ")
    io.flush()
    let r: f64 = io.read_float()
    let area = math.PI * r * r
    print("Area: ")
    println(area)

    let text = strings.concat("r = ", strings.from_int(r as i64))
    println(text)
    strings.free(text)

    let buffer: ptr<u8> = mem.alloc(8) as ptr<u8>
    mem.zero(buffer, 8)
    mem.free(buffer)
    return 0
```

> `strings.from_int` allocates its result. The example above passes it directly
> to `concat`, which does not free its inputs — this intentionally leaks one
> small string for clarity. In a long-running program, keep the intermediate in
> a variable and free it.
