#!/bin/sh
# End-to-end tests for the rynx package manager.
#
# usage: run_rynx_tests.sh <path-to-rynx> <path-to-ryn>
set -u

RYNX=${1:?usage: run_rynx_tests.sh <rynx> <ryn>}
RYN=${2:?usage: run_rynx_tests.sh <rynx> <ryn>}

case "$RYNX" in
    /*) ;;
    *) RYNX=$(cd "$(dirname "$RYNX")" && pwd)/$(basename "$RYNX") ;;
esac
# RYN is passed to rynx through the environment. MSYS2 converts command-line
# arguments for native programs but not environment variables, so keep the
# original Windows path there.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *)
        case "$RYN" in
            /*) ;;
            *) RYN=$(cd "$(dirname "$RYN")" && pwd)/$(basename "$RYN") ;;
        esac
        ;;
esac
export RYN

TMP=$(mktemp -d "${TMPDIR:-/tmp}/rynx-tests-XXXXXX") || exit 1
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
    if grep -q -- "$needle" "$file"; then
        pass
    else
        fail "$description (missing '$needle')"
    fi
}

check_not_contains() {
    description=$1
    needle=$2
    file=$3
    if grep -q -- "$needle" "$file"; then
        fail "$description (unexpected '$needle')"
    else
        pass
    fi
}

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

# version and help ----------------------------------------------------------
"$RYNX" --version > "$TMP/version.out" 2> "$TMP/version.err"
check_status "rynx --version exits 0" 0 $?
check_contains "rynx --version prints the tool version" "rynx 0.2.0" "$TMP/version.out"
check_contains "rynx --version names the Ryn version" "Ryn v0.2" "$TMP/version.out"

"$RYNX" version > "$TMP/version2.out" 2>&1
check_status "rynx version exits 0" 0 $?
check_contains "rynx version prints the tool version" "rynx 0.2.0" "$TMP/version2.out"

"$RYNX" --help > "$TMP/help.out" 2>&1
check_status "rynx --help exits 0" 0 $?
check_contains "rynx help lists init" "init" "$TMP/help.out"
check_contains "rynx help lists install" "install" "$TMP/help.out"

"$RYNX" frobnicate > /dev/null 2>&1
check_status "unknown rynx command exits 2" 2 $?

# init ----------------------------------------------------------------------
mkdir -p "$TMP/demo"
(
    cd "$TMP/demo" || exit 1
    "$RYNX" init demo > "$TMP/init.out" 2> "$TMP/init.err"
    echo $? > "$TMP/init.status"
)
check_status "rynx init exits 0" 0 "$(cat "$TMP/init.status")"
check_contains "rynx init creates src/main.ryn" "fn main" "$TMP/demo/src/main.ryn"
check_contains "rynx init writes the package name" 'name = "demo"' "$TMP/demo/rynx.toml"
check_contains "rynx init reports the manifest" "created rynx.toml" "$TMP/init.out"

(
    cd "$TMP/demo" || exit 1
    "$RYNX" init demo > /dev/null 2>&1
)
check_status "rynx init refuses to overwrite a manifest" 1 $?

# run and build -------------------------------------------------------------
(
    cd "$TMP/demo" || exit 1
    "$RYNX" run > "$TMP/run.out" 2> "$TMP/run.err"
    echo $? > "$TMP/run.status"
)
check_status "rynx run exits with the program status" 0 "$(cat "$TMP/run.status")"
check_output "rynx run executes the entry point" "Hello from demo!" "$(cat "$TMP/run.out")"

(
    cd "$TMP/demo" || exit 1
    "$RYNX" build > /dev/null 2> "$TMP/build.err"
    echo $? > "$TMP/build.status"
)
check_status "rynx build exits 0" 0 "$(cat "$TMP/build.status")"

# dependencies --------------------------------------------------------------
mkdir -p "$TMP/greet"
cat > "$TMP/greet/rynx.toml" <<'RYN'
[package]
name = "greet"
version = "0.2.0"
entry = "src/greet.ryn"

[dependencies]
RYN
mkdir -p "$TMP/greet/src"
cat > "$TMP/greet/src/greet.ryn" <<'RYN'
fn greeting() -> str:
    return "greetings"

fn main() -> i32:
    println(greeting())
    return 0
RYN

(
    cd "$TMP/demo" || exit 1
    "$RYNX" add ../greet > "$TMP/add.out" 2> "$TMP/add.err"
    echo $? > "$TMP/add.status"
)
check_status "rynx add exits 0" 0 "$(cat "$TMP/add.status")"
check_contains "rynx add reports the dependency" "added greet" "$TMP/add.out"
check_contains "rynx add updates the manifest" 'greet = { path = "../greet" }' "$TMP/demo/rynx.toml"
check_contains "rynx add writes a lock file" 'name = "greet"' "$TMP/demo/rynx.lock"
check_contains "lock records the dependency version" 'version = "0.2.0"' "$TMP/demo/rynx.lock"

(
    cd "$TMP/demo" || exit 1
    "$RYNX" list > "$TMP/list.out" 2>&1
)
check_status "rynx list exits 0" 0 $?
check_contains "rynx list shows the project" "demo 0.2.0" "$TMP/list.out"
check_contains "rynx list shows the dependency" "greet" "$TMP/list.out"

(
    cd "$TMP/demo" || exit 1
    "$RYNX" add ../greet > /dev/null 2>&1
)
check_status "rynx add rejects a duplicate dependency" 1 $?

(
    cd "$TMP/demo" || exit 1
    "$RYNX" install > "$TMP/install.out" 2> "$TMP/install.err"
    echo $? > "$TMP/install.status"
)
check_status "rynx install exits 0" 0 "$(cat "$TMP/install.status")"
check_contains "rynx install reports the dependency count" "installed 1 dependency" "$TMP/install.out"

# installed packages are importable from the entry point --------------------
mkdir -p "$TMP/usepkg/src"
cat > "$TMP/usepkg/rynx.toml" <<'RYN'
[package]
name = "usepkg"
version = "0.2.0"
entry = "src/main.ryn"

[dependencies]
greet = { path = "../greet" }
RYN
cat > "$TMP/usepkg/src/main.ryn" <<'RYN'
import greet

fn main() -> i32:
    println(greet.greeting())
    return 0
RYN
(
    cd "$TMP/usepkg" || exit 1
    "$RYNX" install > "$TMP/usepkg_install.out" 2> "$TMP/usepkg_install.err"
    echo $? > "$TMP/usepkg_install.status"
)
check_status "rynx installs the package used by the import test" 0 "$(cat "$TMP/usepkg_install.status")"
(
    cd "$TMP/usepkg" || exit 1
    "$RYNX" run > "$TMP/usepkg_run.out" 2> "$TMP/usepkg_run.err"
    echo $? > "$TMP/usepkg_run.status"
)
check_status "rynx run compiles an installed package import" 0 "$(cat "$TMP/usepkg_run.status")"
check_output "installed package function runs" "greetings" "$(cat "$TMP/usepkg_run.out")"

(
    cd "$TMP/demo" || exit 1
    "$RYNX" remove greet > "$TMP/remove.out" 2> "$TMP/remove.err"
    echo $? > "$TMP/remove.status"
)
check_status "rynx remove exits 0" 0 "$(cat "$TMP/remove.status")"
check_not_contains "rynx remove updates the manifest" "greet" "$TMP/demo/rynx.toml"
check_contains "rynx remove reports the dependency" "removed greet" "$TMP/remove.out"

# errors --------------------------------------------------------------------
(
    cd "$TMP" || exit 1
    "$RYNX" install > /dev/null 2>&1
)
check_status "rynx install outside a project exits 1" 1 $?

(
    cd "$TMP/demo" || exit 1
    "$RYNX" add ../does-not-exist > /dev/null 2>&1
)
check_status "rynx add rejects a missing path" 1 $?

printf 'rynx tests: %d checks, %d failures\n' "$CHECKS" "$FAILS"
[ "$FAILS" -eq 0 ]
