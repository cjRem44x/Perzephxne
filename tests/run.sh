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

run_multifile_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local bin="$TMP/bin/$name.multi"
    local actual="$TMP/out/$name.multi.stdout"
    local compile_err="$TMP/err/$name.multi.compile.stderr"
    local run_err="$TMP/err/$name.multi.run.stderr"
    local expected="$src_dir/stdout"
    local files=()

    printf 'multi %s\n' "$name"
    while IFS= read -r file; do
        files+=("$file")
    done < <(find "$src_dir" -maxdepth 1 -name '*.przp' | sort)

    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "${files[@]}" -o="$bin" >"$TMP/out/$name.multi.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: multi-file compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! "$bin" >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: multi-file run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: multi-file stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

# FFI case: compile a companion shim.c with a real C compiler, then link the
# .przp module(s) against the resulting .o. Exercises `extern fn` signatures
# against independently-compiled native code — the only way to catch calling
# convention (ABI) mismatches, which self-consistent Perzephxne-to-Perzephxne
# calls can never expose.
run_ffi_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local work="$TMP/ffi/$name"
    local shim_o="$work/shim.o"
    local bin="$TMP/bin/$name.ffi"
    local actual="$TMP/out/$name.ffi.stdout"
    local shim_err="$TMP/err/$name.ffi.shim.stderr"
    local compile_err="$TMP/err/$name.ffi.compile.stderr"
    local run_err="$TMP/err/$name.ffi.run.stderr"
    local expected="$src_dir/stdout"
    local files=()

    printf 'ffi   %s\n' "$name"
    mkdir -p "$work"
    cp "$src_dir"/*.przp "$src_dir/shim.c" "$work/"

    if ! clang -c "$work/shim.c" -o "$shim_o" 2>"$shim_err"; then
        printf 'FAIL  %s: shim compile failed\n' "$name" >&2
        sed -n '1,80p' "$shim_err" >&2
        failures=$((failures + 1))
        return
    fi

    while IFS= read -r file; do
        files+=("$file")
    done < <(find "$work" -maxdepth 1 -name '*.przp' | sort)

    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "${files[@]}" "$shim_o" -o="$bin" >"$TMP/out/$name.ffi.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: ffi compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! "$bin" >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: ffi run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: ffi stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

# X11 windowing tests (std/graphics/window). Needs libX11 and a running X
# server, neither of which is guaranteed on every dev/CI machine — skipped
# with a message, not failed, when unavailable. run_x11_setup starts a
# private Xvfb instance once for the whole suite; run_x11_teardown stops it.
X11_DISPLAY=""
X11_XVFB_PID=""

run_x11_setup() {
    if ! command -v Xvfb >/dev/null 2>&1; then
        printf 'skip  x11 tests: Xvfb not installed\n'
        return 1
    fi
    if ! ldconfig -p 2>/dev/null | grep -q "libX11\.so"; then
        printf 'skip  x11 tests: libX11 not installed\n'
        return 1
    fi
    local disp=":77"
    Xvfb "$disp" -screen 0 1024x768x24 >/dev/null 2>&1 &
    X11_XVFB_PID=$!
    local tries=0
    while [ $tries -lt 20 ]; do
        # crude readiness check: Xvfb creates this socket once it's ready
        if [ -S "/tmp/.X11-unix/X77" ]; then break; fi
        sleep 0.2
        tries=$((tries + 1))
    done
    if [ ! -S "/tmp/.X11-unix/X77" ]; then
        printf 'skip  x11 tests: Xvfb did not start\n'
        kill "$X11_XVFB_PID" 2>/dev/null
        X11_XVFB_PID=""
        return 1
    fi
    X11_DISPLAY="$disp"
    return 0
}

run_x11_teardown() {
    if [ -n "$X11_XVFB_PID" ]; then
        kill "$X11_XVFB_PID" 2>/dev/null
        wait "$X11_XVFB_PID" 2>/dev/null
    fi
}

run_x11_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local bin="$TMP/bin/$name.x11"
    local actual="$TMP/out/$name.x11.stdout"
    local compile_err="$TMP/err/$name.x11.compile.stderr"
    local run_err="$TMP/err/$name.x11.run.stderr"
    local expected="$src_dir/stdout"

    printf 'x11   %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src_dir/main.przp" -lX11 -o="$bin" \
            >"$TMP/out/$name.x11.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: x11 compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! DISPLAY="$X11_DISPLAY" timeout 10 "$bin" >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: x11 run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: x11 stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

run_cli_fail() {
    local name="$1"
    local expected="$2"
    shift 2
    local stdout="$TMP/out/$name.cli.stdout"
    local stderr="$TMP/err/$name.cli.stderr"

    printf 'cli   %s\n' "$name"
    if "$@" >"$stdout" 2>"$stderr"; then
        printf 'FAIL  %s: expected command failure\n' "$name" >&2
        failures=$((failures + 1))
        return
    fi

    if ! grep -F "$expected" "$stderr" >/dev/null; then
        printf 'FAIL  %s: stderr did not contain expected text\n' "$name" >&2
        printf 'expected:\n%s\n' "$expected" >&2
        printf 'actual:\n' >&2
        sed -n '1,120p' "$stderr" >&2
        failures=$((failures + 1))
    fi
}

run_cli_fail_in_dir() {
    local name="$1"
    local expected="$2"
    local dir="$3"
    shift 3
    local stdout="$TMP/out/$name.cli.stdout"
    local stderr="$TMP/err/$name.cli.stderr"

    printf 'cli   %s\n' "$name"
    mkdir -p "$dir"
    if (cd "$dir" && "$@" >"$stdout" 2>"$stderr"); then
        printf 'FAIL  %s: expected command failure\n' "$name" >&2
        failures=$((failures + 1))
        return
    fi

    if ! grep -F "$expected" "$stderr" >/dev/null; then
        printf 'FAIL  %s: stderr did not contain expected text\n' "$name" >&2
        printf 'expected:\n%s\n' "$expected" >&2
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

for dir in "$ROOT"/tests/multifile/*; do
    [ -d "$dir" ] || continue
    run_multifile_case "$dir"
done

for dir in "$ROOT"/tests/ffi/*; do
    [ -d "$dir" ] || continue
    run_ffi_case "$dir"
done

if run_x11_setup; then
    for dir in "$ROOT"/tests/x11/*; do
        [ -d "$dir" ] || continue
        run_x11_case "$dir"
    done
    run_x11_teardown
fi

run_init_case

run_cli_fail unknown_command "unknown command 'nope'" "$PRZP" nope
run_cli_fail sac_no_files "przp sac: no input files" "$PRZP" sac
run_cli_fail sac_missing_file "przp: cannot open 'tests/no_such_file.przp'" "$PRZP" sac tests/no_such_file.przp
run_cli_fail_in_dir build_no_manifest "przp build: no przp.toml found" "$TMP/no_manifest_build" "$PRZP" build
run_cli_fail_in_dir run_no_manifest "przp run: no przp.toml found" "$TMP/no_manifest_run" "$PRZP" run

for src in "$ROOT"/tests/fail/*.przp; do
    [ -e "$src" ] || continue
    run_fail_case "$src"
done

if [ "$failures" -ne 0 ]; then
    printf '%d test(s) failed\n' "$failures" >&2
    exit 1
fi

printf 'all tests passed\n'
