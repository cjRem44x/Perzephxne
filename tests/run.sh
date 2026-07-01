#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRZP="$ROOT/compiler/przp"
STDLIB="$ROOT/compiler/std"
TMP="$ROOT/tests/tmp"

failures=0

rm -rf "$TMP"
mkdir -p "$TMP/bin" "$TMP/out" "$TMP/err"

printf 'building compiler...\n'
if ! make -C "$ROOT/compiler" >/dev/null; then
    printf 'compiler build failed\n' >&2
    exit 1
fi

run_success_case() {
    local src="$1"
    local name
    name="$(basename "$src" .przp)"
    local bin="$TMP/bin/$name"
    local actual="$TMP/out/$name.stdout"
    local compile_err="$TMP/err/$name.compile.stderr"
    local run_err="$TMP/err/$name.run.stderr"
    local expected="${src%.przp}.stdout"

    printf 'run   %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src" -o="$bin" >"$TMP/out/$name.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! "$bin" >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

run_fail_case() {
    local src="$1"
    local name
    name="$(basename "$src" .przp)"
    local bin="$TMP/bin/$name"
    local stderr="$TMP/err/$name.stderr"
    local expected="${src%.przp}.stderr"

    printf 'fail  %s\n' "$name"
    if PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src" -o="$bin" >"$TMP/out/$name.stdout" 2>"$stderr"; then
        printf 'FAIL  %s: expected compile failure\n' "$name" >&2
        failures=$((failures + 1))
        return
    fi

    if ! grep -F -f "$expected" "$stderr" >/dev/null; then
        printf 'FAIL  %s: stderr did not contain expected text\n' "$name" >&2
        printf 'expected one of:\n' >&2
        sed -n '1,80p' "$expected" >&2
        printf 'actual:\n' >&2
        sed -n '1,120p' "$stderr" >&2
        failures=$((failures + 1))
    fi
}

run_project_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local work="$TMP/project/$name"
    local actual="$TMP/out/$name.project.stdout"
    local stderr="$TMP/err/$name.project.stderr"
    local expected="$src_dir/stdout"

    printf 'proj  %s\n' "$name"
    mkdir -p "$TMP/project"
    cp -R "$src_dir" "$work"
    rm -f "$work/stdout"

    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$actual" 2>"$stderr"); then
        printf 'FAIL  %s: project run failed\n' "$name" >&2
        sed -n '1,120p' "$stderr" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: project stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

for src in "$ROOT"/tests/run/*.przp; do
    [ -e "$src" ] || continue
    run_success_case "$src"
done

for dir in "$ROOT"/tests/project/*; do
    [ -d "$dir" ] || continue
    run_project_case "$dir"
done

for src in "$ROOT"/tests/fail/*.przp; do
    [ -e "$src" ] || continue
    run_fail_case "$src"
done

if [ "$failures" -ne 0 ]; then
    printf '%d test(s) failed\n' "$failures" >&2
    exit 1
fi

printf 'all tests passed\n'
