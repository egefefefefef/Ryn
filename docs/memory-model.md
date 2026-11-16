# Ryn Memory Model

Ryn has no garbage collector and no hidden allocations for ordinary values. The
model is deliberately close to C, with a few safety nets added by the compiler
and runtime.

## Value semantics

Structs and arrays are **values**:

- Assigning a struct/array copies all of its bytes.
- Passing a struct to a function, and returning one, copies it (including any
  arrays it contains).
- Mutating a copy never affects the original.
- Bare arrays cannot be passed to or returned from functions in 0.1; use a
  pointer or a struct wrapper.

```ryn
struct Point:
    x: i32
    y: i32

fn moved(p: Point, dx: i32) -> Point:
    return Point { x: p.x + dx, y: p.y }

fn main() -> i32:
    let a = Point { x: 1, y: 2 }
    let b = moved(a, 5)
    println(a.x)   # 1
    println(b.x)   # 6
    return 0
```

Scalars are also copied, but the distinction matters for aggregates: there are
no reference-counted or shared objects in the language core.

The generated C implements copies with `memcpy` (`IR_AGG_COPY`), and passes
structs by value in C function signatures. This is a real copy at the ABI level.

## Stack and heap

- Locals (variables, parameters, temporaries) live on the C stack of the current
  function.
- A pointer produced by `&x` refers to that stack storage. It is valid only while
  the function that owns `x` is active.
- Heap memory comes only from `mem.alloc` / `mem.realloc` and from a few runtime
  helpers that return `str` (`strings.concat`, `strings.from_int`,
  `io.read_line`).

Returning a pointer to a local is **not diagnosed**:

```ryn
fn bad() -> ptr<i32>:
    var x: i32 = 1
    return &x          # dangling pointer: do not do this
```

Ryn 0.1 has no borrow checker. Lifetimes are the programmer's responsibility and
are documented here rather than enforced.

## Pointers

```ryn
var value: i32 = 10
let p: ptr<i32> = &value
*p = 20                    # writes through the pointer
let q: ptr<i32> = null     # null pointer
```

Rules enforced by the compiler:

- `&` requires an lvalue: a variable, field, array element, or dereference.
- The binding must be `var`; taking a mutable pointer to a `let` is `E0014`.
- `*` requires a pointer (`E0013`).
- `null` is assignable to any pointer type.
- Pointers can be compared for equality only when their pointee types are
  compatible (identical, or one is `void`).

Not enforced:

- null dereference (crashes at the OS level),
- dangling pointers,
- aliasing rules (pointers may alias freely),
- bounds of pointer indexing,
- pointer arithmetic (not available).

## Manual allocation

```ryn
import mem

fn main() -> i32:
    let p: ptr<u8> = mem.alloc(64) as ptr<u8>
    mem.zero(p, 64)
    mem.free(p)
    return 0
```

| Function | Behaviour |
| --- | --- |
| `mem.alloc(n: u64) -> ptr<void>` | Allocates `n` bytes. `n == 0` requests 1 byte. Panics on failure. Memory is uninitialized. |
| `mem.realloc(p: ptr<void>, n: u64) -> ptr<void>` | Resizes an allocation. Panics on failure. |
| `mem.free(p: ptr<void>)` | Releases memory. `free(null)` is a no-op. Double free is undefined behaviour. |
| `mem.copy(dst, src, n: u64)` | `memmove` semantics; overlapping ranges are safe. |
| `mem.zero(p, n: u64)` | Fills `n` bytes with zero. |

All allocation failures panic with a message instead of returning null.
`mem.free` accepts any pointer type through the `ptr<void>` conversion.

The runtime never allocates implicitly except for the string helpers listed
below and the line reader. In particular, arithmetic, loops, function calls and
aggregate copies do not allocate.

## Strings

`str` is a `const char *` pointing to immutable NUL-terminated bytes.

- **String literals** have static storage duration and must not be freed.
- `strings.concat`, `strings.from_int` and `io.read_line` return **owned**
  heap strings. Release them with `strings.free` when done.
- `strings.free` must only be called on strings that are actually heap owned;
  freeing a literal is undefined behaviour.
- `str` values are immutable: assigning `s[0] = 'x'` is not allowed (indexing a
  string produces a `char` rvalue).
- `==`/`!=` compare string contents; `<`/`>` are not supported.
- Indexing `s[i]` checks `i < len` at run time and panics otherwise.

```ryn
import strings

fn main() -> i32:
    let joined = strings.concat("Ry", "n")
    println(joined)
    strings.free(joined)
    return 0
```

`io.read_line` allocates a fresh buffer per call. The buffer is not invalidated
by later reads, so the caller owns it and can free it with `strings.free`.

## Arrays and bounds

- The length is part of the array type. There is no reallocation of a fixed-size
  array.
- Constant indices are checked at compile time, including negative indices.
- Non-constant array indices are **not** checked at run time in 0.1. Out-of-bounds
  access is undefined behaviour.
- Pointer indexing is never bounds-checked.
- String indexing is checked at run time.

If you need bounds-checked dynamic sequences in 0.1, use an array plus an
explicit length and check before indexing; slices will add a checked fat pointer
in a future version.

## Integer and floating-point semantics

- Signed and unsigned integer arithmetic wraps modulo 2^N (two's complement).
- Division and remainder truncate toward zero.
- `a / 0` and `a % 0` panic at run time (`panic: division by zero`).
- `INT_MIN / -1` saturates to `INT_MIN`; `INT_MIN % -1` is 0.
- Shift counts are masked to `bits - 1` at run time; constant shift counts
  outside `0..63` are compile-time errors.
- Floating-point follows IEEE-754 and does not panic on division by zero.

## Runtime panics and safety nets

Generated programs include these runtime checks:

| Check | Result |
| --- | --- |
| Integer division by zero | `panic: division by zero` |
| String index out of bounds | `panic: string index N is out of bounds (length M)` |
| Allocation failure | `panic: out of memory allocating N bytes` |
| `assert(false)` | `panic: assertion failed` |
| Failed input parsing | `panic: expected an integer, got '...'` |
| Explicit `panic(msg)` | `panic: msg` |

A panic writes to standard error, flushes standard output, and exits with
status 1. There are no exceptions and no unwinding.

## Interoperability with C

`extern fn` declarations call C functions directly. Ryn types map to the C types
in [types.md](types.md); structs use the C ABI; `str` is `const char *`. The
generated C file can be compiled and linked with arbitrary C code, provided the
declarations match. Passing owning strings to C transfers no ownership
information — decide explicitly who frees what.

## Summary of guarantees (0.1)

Guaranteed:

- type and argument correctness,
- definite assignment before reads,
- compile-time constant array bounds checks,
- division-by-zero and string-index checks,
- immutable string contents and immutable bindings.

Not guaranteed:

- memory safety of raw pointers,
- null safety,
- bounds safety for arrays/pointers,
- lifetime safety for pointers to stack values,
- thread safety (the runtime and generated code are single-threaded).
