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

run_project_fail_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local work="$TMP/project_fail/$name"
    local stderr="$TMP/err/$name.project-fail.stderr"
    local expected="$src_dir/stderr"

    printf 'pfail %s\n' "$name"
    mkdir -p "$TMP/project_fail"
    cp -R "$src_dir" "$work"
    rm -f "$work/stderr"

    if (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" build >"$TMP/out/$name.project-fail.stdout" 2>"$stderr"); then
        printf 'FAIL  %s: expected project build failure\n' "$name" >&2
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

run_init_case() {
    local work="$TMP/init/current"
    local stdout="$TMP/out/init.stdout"
    local stderr="$TMP/err/init.stderr"

    printf 'init  current\n'
    mkdir -p "$work"
    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" init >"$stdout" 2>"$stderr"); then
        printf 'FAIL  init current: init failed\n' >&2
        sed -n '1,120p' "$stderr" >&2
        failures=$((failures + 1))
        return
    fi
    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >>"$stdout" 2>>"$stderr"); then
        printf 'FAIL  init current: run failed\n' >&2
        sed -n '1,120p' "$stderr" >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -F "Hello from current!" "$stdout" >/dev/null; then
        printf 'FAIL  init current: missing run output\n' >&2
        sed -n '1,120p' "$stdout" >&2
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

for dir in "$ROOT"/tests/project-fail/*; do
    [ -d "$dir" ] || continue
    run_project_fail_case "$dir"
done

run_init_case

for src in "$ROOT"/tests/fail/*.przp; do
    [ -e "$src" ] || continue
    run_fail_case "$src"
done

if [ "$failures" -ne 0 ]; then
    printf '%d test(s) failed\n' "$failures" >&2
    exit 1
fi

printf 'all tests passed\n'
