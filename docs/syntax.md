# A Guide to Ryn Syntax

This guide is a hands-on tour. The precise rules live in
[language-spec.md](language-spec.md); the grammar in
[grammar.ebnf](grammar.ebnf).

## Files and statements

Ryn source files use the `.ryn` extension and are UTF-8. A file is a sequence of
top-level declarations. Statements inside a function are separated by newlines —
no semicolons:

```ryn
fn main() -> i32:
    let x = 1
    let y = 2
    println(x + y)
    return 0
```

Indentation defines blocks. A `:` opens a block and the following lines are
indented with spaces. **Tabs are rejected.**

```ryn
fn classify(n: i32) -> str:
    if n < 0:
        return "negative"
    else if n == 0:
        return "zero"
    else:
        return "positive"
```

Comments start with `#` and run to the end of the line. Blank lines are ignored.

## Variables

```ryn
let name = "Ryn"          # immutable, inferred as str
let count: i32 = 3        # immutable, explicit type
var total: i64 = 0        # mutable
total += 1                # compound assignment
```

`let` bindings cannot be reassigned; use `var` when mutation is intended. The
compiler tracks definite initialization: a variable declared without a value
cannot be read until every path assigns it.

## Primitive types

```ryn
let a: i8 = -12
let b: u32 = 4_000_000_000
let c: f32 = 1.5
let d: f64 = 6.02e23
let flag: bool = true
let letter: char = 'R'
let text: str = "hello"
```

Integer literals adapt to the annotated type (`let x: u8 = 255` works, `256`
does not). Arithmetic requires both operands to have the same type; literals
adapt, but variables need an explicit cast when mixing:

```ryn
let small: i32 = 10
let big: i64 = small as i64 + 1
```

## Structs and enums

```ryn
struct Vector2:
    x: f32
    y: f32

enum Color:
    Red
    Green = 5
    Blue

fn main() -> i32:
    var v = Vector2 { x: 1.0, y: 2.0 }
    v.x = 3.0
    let color = Color.Blue
    println(color as i32)     # 6
    return 0
```

Struct literals use braces with `field: value` pairs, even though statement
blocks do not use braces. Every field must be initialized.

## Control flow

```ryn
if x > 0 and x < 100:
    println("in range")
else:
    println("out of range")

while n < 10:
    n += 1

for i in 0..5:       # 0, 1, 2, 3, 4
    print(i)

for i in 0..=5:      # 0, 1, 2, 3, 4, 5
    print(i)
```

Conditions must be `bool`; writing `if x:` where `x` is an integer is an error.
Use `break` and `continue` inside loops.

## Functions

```ryn
fn add(a: i32, b: i32) -> i32:
    return a + b

fn log(message: str):
    println(message)
```

The return type can be omitted for `void` functions. Every non-void function must
return on every path.

Function values are supported:

```ryn
fn square(x: i32) -> i32:
    return x * x

fn apply(f: fn(i32) -> i32, value: i32) -> i32:
    return f(value)

fn main() -> i32:
    println(apply(square, 7))
    let callback: fn(i32) -> i32 = square
    println(callback(3))
    return 0
```

## Arrays and indexing

```ryn
let numbers: [i32; 4] = [1, 2, 3, 4]
let zeros: [u8; 16] = [0; 16]
println(numbers[2])

var mutable: [i32; 4] = [1, 2, 3, 4]
mutable[2] = 30
```

Constant out-of-bounds indices are compile-time errors. Runtime bounds checks for
arrays are not generated in 0.1 — see [memory-model.md](memory-model.md).

## Pointers

```ryn
import mem

fn main() -> i32:
    var value: i32 = 1
    let p: ptr<i32> = &value
    *p = 2
    println(value)              # 2

    let heap: ptr<i32> = mem.alloc(4) as ptr<i32>
    *heap = 9
    println(*heap)
    mem.free(heap)
    return 0
```

`&` takes the address of a mutable variable, field or element. `*` dereferences.
Fields of a `ptr<Struct>` can be accessed directly with `.`:

```ryn
var v = Vector2 { x: 1.0, y: 2.0 }
let p = &v
p.x = 5.0
```

Pointer arithmetic is not part of 0.1; use indexing (`p[i]`).

## Strings

```ryn
import strings

let greeting = "Hello"
println(greeting)
println(strings.len(greeting))          # 5
println(strings.concat(greeting, "!"))  # Hello!
println(greeting[0])                    # H
println(greeting == "Hello")            # true
```

String indexing is bounds-checked. `strings.concat` allocates; release the result
with `strings.free` when it is no longer needed:

```ryn
import strings

fn main() -> i32:
    let joined = strings.concat("Ry", "n")
    println(joined)
    strings.free(joined)
    return 0
```

## Imports and the standard library

```ryn
import math
import io

fn main() -> i32:
    println(math.sqrt(2.0))
    print("Your name: ")
    io.flush()
    let name = io.read_line()
    println(name)
    return 0
```

`core` is available without an import (`print`, `println`, `panic`, `assert`,
`assert_msg`, `len`). See [modules.md](modules.md) for the full list.

## Printing

`print` and `println` are overloaded per type; they take exactly one argument:

```ryn
print(42)          # integer
print(1.5)         # float, prints 1.5
print(true)        # bool
print('R')         # char
print("text")      # str
print(some_ptr)    # address or "null"
println("")        # blank line
```

Floats always include a decimal point (`1.0`, not `1`). Null pointers print as
`null`.

## Common mistakes

| Symptom | Cause |
| --- | --- |
| `E0010` | A tab in indentation |
| `E0009` | Missing `:` before a block, or a stray `;` |
| `E0003` | Mixing types without `as`, e.g. `i32 + i64` |
| `E0004` | Assigning to a `let` binding |
| `E0029` | Reading a variable that may be uninitialized |
| `E0014` | Taking `&x` of an immutable binding |
| `E0037` | Using a feature reserved for a future version (slices, generics) |
