#!/bin/sh
# End-to-end tests for the ryn command line interface.
#
# usage: run_cli_tests.sh <path-to-ryn> <path-to-runtime-dir>
set -u

RYN=${1:?usage: run_cli_tests.sh <ryn> <runtime-dir> [examples-dir]}
RUNTIME_DIR=${2:?usage: run_cli_tests.sh <ryn> <runtime-dir> [examples-dir]}
EXAMPLES_DIR=${3:-}
CC_BIN=${CC:-cc}
EXE=
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) EXE=.exe ;;
esac

# Make both paths absolute: some tests change into a scratch directory.
case "$RYN" in
    /*) ;;
    *) RYN=$(cd "$(dirname "$RYN")" && pwd)/$(basename "$RYN") ;;
esac
case "$RUNTIME_DIR" in
    /*) ;;
    *) RUNTIME_DIR=$(cd "$RUNTIME_DIR" && pwd) ;;
esac

TMP=$(mktemp -d "${TMPDIR:-/tmp}/ryn-cli-XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT

CHECKS=0
FAILS=0

pass() {
    CHECKS=$((CHECKS + 1))
}

fail() {
    CHECKS=$((CHECKS + 1))
    FAILS=$((FAILS + 1))
    printf 'FAIL: %s\n' "$*" >&2
}

check_status() {
    description=$1
    expected=$2
    actual=$3
    if [ "$actual" -eq "$expected" ]; then
        pass
    else
        fail "$description (exit $actual, expected $expected)"
    fi
}

check_contains() {
    description=$1
    needle=$2
    file=$3
    if grep -q "$needle" "$file"; then
        pass
    else
        fail "$description (missing '$needle')"
    fi
}

# Strip carriage returns so text-mode stdout on Windows compares equal.
normalize_text() {
    printf '%s' "$1" | tr -d '\r'
}

check_output() {
    description=$1
    expected=$2
    actual=$3
    if [ "$(normalize_text "$actual")" = "$(normalize_text "$expected")" ]; then
        pass
    else
        fail "$description (got '$actual', expected '$expected')"
    fi
}

cat > "$TMP/hello.ryn" <<'RYN'
fn main() -> i32:
    println("Hello, Ryn!")
    return 0
RYN

cat > "$TMP/exit7.ryn" <<'RYN'
fn main() -> i32:
    return 7
RYN

cat > "$TMP/bad.ryn" <<'RYN'
fn main() -> i32:
    println(missing_name)
    return 0
RYN

cat > "$TMP/panic.ryn" <<'RYN'
fn main() -> i32:
    var zero: i32 = 0
    println(1 / zero)
    return 0
RYN

cat > "$TMP/stdlib.ryn" <<'RYN'
import math
import strings
import mem

fn main() -> i32:
    println(len("Ryn"))
    println(math.sqrt(9.0))
    println(math.abs(-7))
    println(math.max(3, 8))
    let text = strings.from_int(-42)
    println(text)
    strings.free(text)
    let raw: ptr<u8> = mem.alloc(4) as ptr<u8>
    mem.zero(raw, 4)
    println(raw[3] as i32)
    mem.free(raw)
    return 0
RYN

cat > "$TMP/extern.ryn" <<'RYN'
extern fn puts(s: str) -> i32

fn main() -> i32:
    puts("from C")
    return 0
RYN

# version and help ----------------------------------------------------------
"$RYN" version > "$TMP/version.txt" 2>&1
check_status "version exits 0" 0 $?
check_contains "version prints the compiler version" "ryn 0.2.0" "$TMP/version.txt"

"$RYN" --version > "$TMP/version_flag.txt" 2>&1
check_status "--version exits 0" 0 $?
check_contains "--version prints the compiler version" "ryn 0.2.0" "$TMP/version_flag.txt"

"$RYN" -V > "$TMP/version_short.txt" 2>&1
check_status "-V exits 0" 0 $?
check_contains "-V prints the compiler version" "ryn 0.2.0" "$TMP/version_short.txt"

"$RYN" help > "$TMP/help.txt" 2>&1
check_status "help exits 0" 0 $?
check_contains "help lists build" "build" "$TMP/help.txt"

# check ---------------------------------------------------------------------
"$RYN" check "$TMP/hello.ryn" > "$TMP/check.out" 2> "$TMP/check.err"
check_status "check accepts a valid program" 0 $?
check_output "check is silent on success" "" "$(cat "$TMP/check.out")"

"$RYN" check "$TMP/bad.ryn" > "$TMP/bad.out" 2> "$TMP/bad.err"
check_status "check rejects an invalid program" 1 $?
check_contains "diagnostic contains an error code" "E0001" "$TMP/bad.err"
check_contains "diagnostic contains a caret" "\\^" "$TMP/bad.err"

"$RYN" check "$TMP/not-there.ryn" > /dev/null 2> "$TMP/missing.err"
check_status "missing input exits 1" 1 $?
check_contains "missing input is reported" "cannot read" "$TMP/missing.err"

# dumps ---------------------------------------------------------------------
"$RYN" check --dump-tokens "$TMP/hello.ryn" > "$TMP/tokens.txt" 2>/dev/null
check_status "dump-tokens exits 0" 0 $?
check_contains "dump-tokens shows identifiers" "identifier" "$TMP/tokens.txt"

"$RYN" check --dump-ast "$TMP/hello.ryn" > "$TMP/ast.txt" 2>/dev/null
check_contains "dump-ast shows main" "fn main" "$TMP/ast.txt"

"$RYN" check --emit-ir "$TMP/hello.ryn" > "$TMP/ir.txt" 2>/dev/null
check_contains "emit-ir shows the entry function" "fn ryn_main" "$TMP/ir.txt"

# run -----------------------------------------------------------------------
"$RYN" run "$TMP/hello.ryn" > "$TMP/run.out" 2> "$TMP/run.err"
check_status "run exits with the program status" 0 $?
check_output "run produces the program output" "Hello, Ryn!" "$(cat "$TMP/run.out")"

"$RYN" run "$TMP/exit7.ryn" > /dev/null 2>&1
check_status "run propagates the program exit code" 7 $?

"$RYN" run "$TMP/panic.ryn" > /dev/null 2> "$TMP/panic.err"
check_status "a runtime panic exits 1" 1 $?
check_contains "a runtime panic is reported" "panic:" "$TMP/panic.err"

"$RYN" run "$TMP/hello.ryn" -- one two > /dev/null 2>&1
check_status "run accepts program arguments" 0 $?

# standard library and extern C ---------------------------------------------
"$RYN" run "$TMP/stdlib.ryn" > "$TMP/stdlib.out" 2> "$TMP/stdlib.err"
check_status "stdlib program runs" 0 $?
check_output "stdlib results are correct" "3
3.0
7
8
-42
0" "$(cat "$TMP/stdlib.out")"

"$RYN" run --include stdio.h "$TMP/extern.ryn" > "$TMP/extern.out" 2> "$TMP/extern.err"
check_status "extern C call runs" 0 $?
check_output "extern C call output" "from C" "$(cat "$TMP/extern.out")"

# build ---------------------------------------------------------------------
"$RYN" build "$TMP/hello.ryn" -o "$TMP/hello_bin$EXE" > "$TMP/build.out" 2> "$TMP/build.err"
check_status "build produces an executable" 0 $?
check_output "built program runs" "Hello, Ryn!" "$("$TMP/hello_bin$EXE")"

"$RYN" build --release "$TMP/hello.ryn" -o "$TMP/hello_rel$EXE" > /dev/null 2>&1
check_status "release build works" 0 $?

# transpile -----------------------------------------------------------------
"$RYN" transpile "$TMP/hello.ryn" -o "$TMP/hello.c" > /dev/null 2>&1
check_status "transpile produces C" 0 $?
check_contains "generated C includes the runtime" "ryn_runtime.h" "$TMP/hello.c"
"$CC_BIN" -std=c11 -I "$RUNTIME_DIR" "$TMP/hello.c" "$RUNTIME_DIR"/*.c -lm \
    -o "$TMP/hello_from_c$EXE" > "$TMP/cc.err" 2>&1
check_status "generated C compiles with a C compiler" 0 $?
check_output "C-compiled program runs" "Hello, Ryn!" "$("$TMP/hello_from_c$EXE")"

# default output names ------------------------------------------------------
mkdir -p "$TMP/defaults"
cp "$TMP/hello.ryn" "$TMP/defaults/hello.ryn"
(
    cd "$TMP/defaults" || exit 1
    "$RYN" transpile hello.ryn > /dev/null 2>&1
    "$RYN" build hello.ryn > /dev/null 2>&1
)
if [ -f "$TMP/defaults/hello.c" ]; then default_c=0; else default_c=1; fi
if [ -f "$TMP/defaults/hello$EXE" ]; then default_bin=0; else default_bin=1; fi
check_status "transpile derives hello.c" 0 "$default_c"
check_status "build derives hello" 0 "$default_bin"

# user modules --------------------------------------------------------------
mkdir -p "$TMP/mods"
cat > "$TMP/mods/main.ryn" <<'RYN'
import geometry

fn main() -> i32:
    let p = geometry.Point { x: 3, y: 4 }
    println(geometry.distance(p))
    println(geometry.scaled(p, 2))
    return 0
RYN
cat > "$TMP/mods/geometry.ryn" <<'RYN'
import math
import scaler

struct Point:
    x: i32
    y: i32

fn distance(p: Point) -> f64:
    return math.sqrt((p.x * p.x + p.y * p.y) as f64)

fn scaled(p: Point, factor: i32) -> i32:
    return scaler.scale(p.x + p.y, factor)
RYN
cat > "$TMP/mods/scaler.ryn" <<'RYN'
fn scale(value: i32, factor: i32) -> i32:
    return value * factor
RYN
"$RYN" run "$TMP/mods/main.ryn" > "$TMP/mods.out" 2> "$TMP/mods.err"
check_status "local modules run" 0 $?
check_output "local modules resolve names and types" "5.0
14" "$(cat "$TMP/mods.out")"

mkdir -p "$TMP/mods/.rynx/packages/units/src"
cat > "$TMP/mods/package_main.ryn" <<'RYN'
import units

fn main() -> i32:
    println(units.triple(4))
    return 0
RYN
cat > "$TMP/mods/.rynx/packages/units/src/lib.ryn" <<'RYN'
fn triple(value: i32) -> i32:
    return value * 3
RYN
"$RYN" run "$TMP/mods/package_main.ryn" > "$TMP/package_mods.out" 2> "$TMP/package_mods.err"
check_status "packages in .rynx/packages are importable" 0 $?
check_output "package modules run" "12" "$(cat "$TMP/package_mods.out")"

mkdir -p "$TMP/mods/extra/foo/src"
cat > "$TMP/mods/module_path_main.ryn" <<'RYN'
import foo

fn main() -> i32:
    println(foo.twenty_one())
    return 0
RYN
cat > "$TMP/mods/extra/foo/src/lib.ryn" <<'RYN'
fn twenty_one() -> i32:
    return 21
RYN
"$RYN" run --module-path "$TMP/mods/extra" "$TMP/mods/module_path_main.ryn" > "$TMP/module_path.out" 2> "$TMP/module_path.err"
check_status "--module-path modules run" 0 $?
check_output "--module-path is searched" "21" "$(cat "$TMP/module_path.out")"

mkdir -p "$TMP/envmods/bar/src"
cat > "$TMP/mods/env_main.ryn" <<'RYN'
import bar

fn main() -> i32:
    println(bar.five())
    return 0
RYN
cat > "$TMP/envmods/bar/src/lib.ryn" <<'RYN'
fn five() -> i32:
    return 5
RYN
(
    cd "$TMP/mods" || exit 1
    RYN_MODULE_PATH=../envmods "$RYN" run env_main.ryn > "$TMP/envmods.out" 2> "$TMP/envmods.err"
    echo $? > "$TMP/envmods.status"
)
check_status "RYN_MODULE_PATH modules run" 0 "$(cat "$TMP/envmods.status")"
check_output "RYN_MODULE_PATH is searched" "5" "$(cat "$TMP/envmods.out")"

mkdir -p "$TMP/modcycle"
cat > "$TMP/modcycle/a.ryn" <<'RYN'
import b

fn main() -> i32:
    return 0
RYN
cat > "$TMP/modcycle/b.ryn" <<'RYN'
import a

fn helper() -> i32:
    return 1
RYN
"$RYN" check "$TMP/modcycle/a.ryn" > /dev/null 2> "$TMP/modcycle.err"
check_status "import cycles are rejected" 1 $?
check_contains "import cycle is reported" "E0040" "$TMP/modcycle.err"

mkdir -p "$TMP/aliasmask/a" "$TMP/aliasmask/b"
cat > "$TMP/aliasmask/main.ryn" <<'RYN'
import a
import b

fn main() -> i32:
    println(a.value())
    println(b.value())
    return 0
RYN
cat > "$TMP/aliasmask/a/mod.ryn" <<'RYN'
import util

fn value() -> i32:
    return util.one() + 10
RYN
cat > "$TMP/aliasmask/a/util.ryn" <<'RYN'
fn one() -> i32:
    return 1
RYN
cat > "$TMP/aliasmask/b/mod.ryn" <<'RYN'
import util

fn value() -> i32:
    return util.one() + 20
RYN
cat > "$TMP/aliasmask/b/util.ryn" <<'RYN'
fn one() -> i32:
    return 2
RYN
"$RYN" run "$TMP/aliasmask/main.ryn" > "$TMP/aliasmask.out" 2> "$TMP/aliasmask.err"
check_status "modules with the same alias in different files run" 0 $?
check_output "module aliases do not collide" "11
22" "$(cat "$TMP/aliasmask.out")"

cat > "$TMP/mods/missing_import.ryn" <<'RYN'
import nowhere

fn main() -> i32:
    return 0
RYN
"$RYN" check "$TMP/mods/missing_import.ryn" > /dev/null 2> "$TMP/nowhere.err"
check_status "missing modules are rejected" 1 $?
check_contains "missing module is reported" "unknown module 'nowhere'" "$TMP/nowhere.err"

# usage errors --------------------------------------------------------------
"$RYN" frobnicate > /dev/null 2>&1
check_status "unknown command exits 2" 2 $?

"$RYN" check > /dev/null 2>&1
check_status "missing input exits 2" 2 $?

"$RYN" check --target bogus "$TMP/hello.ryn" > /dev/null 2>&1
check_status "unknown target exits 2" 2 $?

"$RYN" check --no-such-option "$TMP/hello.ryn" > /dev/null 2>&1
check_status "unknown option exits 2" 2 $?

# examples ------------------------------------------------------------------
if [ -n "$EXAMPLES_DIR" ]; then
    for example in "$EXAMPLES_DIR"/*.ryn; do
        [ -e "$example" ] || continue
        name=$(basename "$example")
        case "$name" in
            io_sum.ryn)
                printf '3\n5\n10\n20\n' | "$RYN" run "$example" > /dev/null 2>&1
                ;;
            *)
                "$RYN" run "$example" > /dev/null 2>&1
                ;;
        esac
        check_status "example $name runs" 0 $?
    done
    if [ -f "$EXAMPLES_DIR/modules/main.ryn" ]; then
        "$RYN" run "$EXAMPLES_DIR/modules/main.ryn" > "$TMP/example_modules.out" 2>&1
        check_status "example modules/main.ryn runs" 0 $?
        check_output "example modules/main.ryn resolves imports" "5.0
point" "$(cat "$TMP/example_modules.out")"
    fi
fi

printf 'cli tests: %d checks, %d failures\n' "$CHECKS" "$FAILS"
[ "$FAILS" -eq 0 ]
