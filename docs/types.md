# Ryn Type System

Ryn is statically typed. Every expression has a type determined at compile time;
there are no implicit dynamic conversions and no `any` type.

## Primitive types

| Ryn type | C type in generated code | Size | Range / meaning |
| --- | --- | --- | --- |
| `i8` | `int8_t` | 1 byte | −128 … 127 |
| `i16` | `int16_t` | 2 bytes | −32 768 … 32 767 |
| `i32` | `int32_t` | 4 bytes | −2 147 483 648 … 2 147 483 647 |
| `i64` | `int64_t` | 8 bytes | −9 223 372 036 854 775 808 … 9 223 372 036 854 775 807 |
| `u8` | `uint8_t` | 1 byte | 0 … 255 |
| `u16` | `uint16_t` | 2 bytes | 0 … 65 535 |
| `u32` | `uint32_t` | 4 bytes | 0 … 4 294 967 295 |
| `u64` | `uint64_t` | 8 bytes | 0 … 18 446 744 073 709 551 615 |
| `f32` | `float` | 4 bytes | IEEE-754 binary32 |
| `f64` | `double` | 8 bytes | IEEE-754 binary64 |
| `bool` | `bool` | 1 byte | `true` / `false` |
| `char` | `uint8_t` | 1 byte | A single byte, printed as a character |
| `str` | `const char *` | pointer | Immutable NUL-terminated UTF-8 bytes |
| `void` | `void` | — | Function results only |

`bool` and `char` are **not** integers. They compare with `==`/`!=` (`char` also
supports ordering), and arithmetic requires an explicit `as` cast to an integer
type and back.

## Untyped literals

Integer and float literals start out "untyped" and adapt to the context:

```ryn
let a: u8 = 200          # untyped integer becomes u8
let b: f64 = 2           # untyped integer becomes f64
let c = 2.5              # no context: f64
let d = 7                # no context: i32
let e: i64 = a           # u8 widens to i64 (signedness mismatch? see below)
```

The last line is an error: widening requires matching signedness. Use
`let e: i64 = a as i64`.

Literals are range-checked against the target type (`E0025`); `let x: i8 = 200`
does not compile.

## Implicit conversions

Only these are implicit:

1. Untyped literals to a matching numeric type (or integer literal to float).
2. Signed → wider signed: `i8 → i16 → i32 → i64`.
3. Unsigned → wider unsigned: `u8 → u16 → u32 → u64`.
4. `null` → any `ptr<T>`.
5. `ptr<T>` → `ptr<void>`.

Everything else needs `as`. In particular:

- no narrowing (`i64 → i32`) without a cast;
- no signed/unsigned mixing;
- no integer ↔ float conversion between *typed* values (literals adapt);
- no `f32 → f64` widening without a cast;
- no `int → bool` conversion, and no truthiness in conditions.

## Explicit casts

`value as T` is checked by the compiler. Allowed combinations:

| From | To |
| --- | --- |
| any numeric | any numeric |
| `bool` | numeric, pointer |
| numeric, pointer | `bool` (zero becomes false) |
| `char` | numeric, pointer, `char` |
| numeric, pointer | `char` (low byte) |
| pointer | pointer, integer, `bool` |
| integer | pointer |
| `null` | pointer |
| enum | integer, float |
| integer | enum |

Disallowed: anything involving `str`, structs or arrays, and `void`.

Casts do not check ranges or validity. Casting a truncated integer, or an
arbitrary integer to a pointer, is the programmer's responsibility.

## Arrays

```ryn
let a: [i32; 4] = [1, 2, 3, 4]
let b: [f64; 2] = [1.0, 2.0]
let c: [u8; 16] = [0; 16]
```

- The length is part of the type (`[i32; 4]` ≠ `[i32; 8]`).
- Arrays are value types; assignment copies them.
- Bare arrays cannot be function parameters or return types in 0.2. Pass a
  `ptr<T>` (plus a length) or wrap the array in a struct.
- Elements are stored contiguously with C layout.
- Indexing uses `a[i]`, where `i` is any integer type.
- Negative constant indices and constant out-of-range indices are compile-time
  errors (`E0035`). Non-constant indices are not bounds-checked in 0.2.

Array literals support `[a, b, c]` with an optional trailing comma, the repeat
form `[value; N]`, and the empty literal `[]` (whose type must come from an
annotation).

## Pointers

```ryn
ptr<i32>        # pointer to a mutable i32
ptr<void>       # untyped pointer, as returned by mem.alloc/realloc
ptr<Point>      # pointer to a struct
```

- `&lvalue` produces a pointer; the referenced binding must be `var`.
- `*p` reads or writes the pointee.
- `p[i]` indexes pointer memory; no bounds checks are emitted.
- Field access through a pointer to a struct auto-dereferences: `p.x`.
- `null` is the null pointer value; it compares with `==`/`!=` and prints `null`.
- Pointer comparison requires compatible pointee types (identical, or one of them
  `void`); ordering pointers is not supported.
- Pointer arithmetic is not supported in 0.2.

See [memory-model.md](memory-model.md) for lifetimes and ownership.

## Structs

```ryn
struct Line:
    start: Point
    end: Point
```

- Named fields, declaration order is layout order.
- Value type: assignment, argument passing and returns copy the entire struct.
- The generated C uses a plain `struct`, so padding follows the platform C ABI.
  Ryn has no `packed` attribute in 0.2.
- Fields may be primitives, pointers, arrays, other structs, enums or function
  values.
- Recursive structs must use a pointer (`next: ptr<Node>`); recursion by value is
  rejected.

Struct literals initialize every field:

```ryn
let p = Point { x: 1, y: 2 }
```

## Enums

```ryn
enum Color:
    Red
    Green = 5
    Blue
```

- Variants are `i32` by default; the enum becomes `i64` if any variant value is
  outside `i32`.
- Variants are referenced with qualification: `Color.Red`.
- Enums can be compared, cast to/from integers, and printed after a cast.
- Ryn 0.2 enums are plain integer constants: they cannot carry payloads.

## Function types

```ryn
fn(i32, i32) -> i32
fn(str)
```

A function type is a first-class value, represented in C by a typedef'd function
pointer. Function names coerce to function values; function values can be stored
in variables and struct fields, passed as parameters, and returned.

Calls through a function value check the argument types and count. There are no
closures: function values carry no captured environment.

## `void` and `!` (error)

`void` is only valid as a function result. Using a `void` expression as a value
is `E0015`.

The compiler has an internal error type used to suppress cascading diagnostics;
it is not nameable in source code.

## Not implemented in 0.2

- **Slices** `[]T`: recognized and rejected with `E0037`. Use `[T; N]` arrays with
  an accompanying length, or a pointer plus explicit count.
- **Generics**: generic parameter lists are recognized and rejected with
  `E0037`.
- **Sum types / payload enums**: enums are integer constants only.
- **Struct packing / unions / bitfields**.

These are tracked in the roadmap in the [README](../README.md#roadmap). The type
system representation (`TypeKind` in `include/types.h`) is extensible and is the
place where slices will be added.
