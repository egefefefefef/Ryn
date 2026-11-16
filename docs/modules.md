# Modules and the Standard Library

## Imports

```ryn
import math
```

An import binds a built-in module name in the current file. Members are used
with a qualified name:

```ryn
println(math.sqrt(2.0))
println(math.PI)
```

- Imports must precede all other declarations (`E0033`).
- Only the five built-in modules below exist in 0.1; `import foo` for anything
  else is `E0026`.
- There are no file-based modules or packages yet.
- Importing the same module twice is `E0008` (duplicate declaration).

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

In 0.1 there is no file I/O; only standard input/output are exposed.

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
