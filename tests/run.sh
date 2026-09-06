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

    local stdin_file="${src%.przp}.stdin"
    if [ ! -f "$stdin_file" ]; then stdin_file=/dev/null; fi

    if ! "$bin" <"$stdin_file" >"$actual" 2>"$run_err"; then
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

# Like run_success_case, but links -lz — for tests/run/*.przp files that
# import std/image (its uncompress() extern needs zlib even when only its
# GIF decoder, which needs no zlib itself, is actually exercised). Gated
# on libz's presence like tests/zip/gl's own link-time deps, though zlib
# is ubiquitous enough this should essentially never skip in practice.
run_lz_case() {
    local src="$1"
    local name
    name="$(basename "$src" .przp)"
    local bin="$TMP/bin/$name"
    local actual="$TMP/out/$name.stdout"
    local compile_err="$TMP/err/$name.compile.stderr"
    local run_err="$TMP/err/$name.run.stderr"
    local expected="${src%.przp}.stdout"

    printf 'run   %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src" -lz -o="$bin" >"$TMP/out/$name.compile.stdout" 2>"$compile_err"; then
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

# std/audio tests (libmpg123 + ALSA FFI). Needs both libraries' runtime
# .so's — gated separately below, skipped (not failed) when absent.
# Playback goes to ALSA's "null" PCM device, so no real sound hardware is
# needed (see run_gl_case's Xvfb for the analogous graphics story).
run_audio_case() {
    local src="$1"
    local name
    name="$(basename "$src" .przp)"
    local bin="$TMP/bin/$name"
    local actual="$TMP/out/$name.stdout"
    local compile_err="$TMP/err/$name.compile.stderr"
    local run_err="$TMP/err/$name.run.stderr"
    local expected="${src%.przp}.stdout"

    printf 'audio %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src" -lasound -lmpg123 -o="$bin" >"$TMP/out/$name.compile.stdout" 2>"$compile_err"; then
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

# End-to-end `przp add` + ensure_deps auto-fetch + `dep/` import resolution
# (see book/src/build-system.md#dependencies). Needs `git`, gated (skipped,
# not failed) like the GL/audio tests are for their own runtime deps —
# unlike those, there's no way to test this against a real, hardware-free
# "null device" equivalent, so instead a whole fake przp_dep_greeter repo
# is built as a local bare repo under $TMP and PRZP_DEPS_GIT_BASE points at
# it with a file:// URL — real git clone/fetch/checkout behavior, but no
# actual network access, the same reasoning std_audio.przp uses ALSA's
# "null" device for.
run_deps_case() {
    if ! command -v git >/dev/null 2>&1; then
        printf 'skip  deps: git not installed\n'
        return
    fi
    printf 'deps  add_and_autofetch\n'

    local base="$TMP/deps_fixture"
    rm -rf "$base"
    mkdir -p "$base/org" "$base/srcstage" "$base/proj/src"

    if ! (
        cd "$base/srcstage" &&
        git init --quiet &&
        git config user.email test@test.com &&
        git config user.name test &&
        printf 'fn greet() -> i32 { ret 7 }\n' > greeter.przp &&
        git add greeter.przp &&
        git commit --quiet -m init &&
        git branch -M main
    ); then
        printf 'FAIL  deps: could not build fixture source repo\n' >&2
        failures=$((failures + 1))
        return
    fi

    git init --quiet --bare "$base/org/przp_dep_greeter.git"
    if ! (cd "$base/srcstage" && git remote add origin "$base/org/przp_dep_greeter.git" && git push --quiet origin main); then
        printf 'FAIL  deps: could not push fixture source repo to bare remote\n' >&2
        failures=$((failures + 1))
        return
    fi
    git --git-dir="$base/org/przp_dep_greeter.git" symbolic-ref HEAD refs/heads/main

    cat > "$base/proj/przp.toml" <<'EOF'
[package]
name = "depsfixtureproj"

[build]
entry = "src/main.przp"
EOF
    cat > "$base/proj/src/main.przp" <<'EOF'
import(g = "dep/greeter")

fn main() -> i32 {
    r: i32 = g.greet()
    @pf("r={r}\n")
    ret 0
}
EOF

    local add_out="$TMP/out/deps_add.stdout"
    local add_err="$TMP/err/deps_add.stderr"
    if ! (cd "$base/proj" && PRZP_STDLIB="$STDLIB" PRZP_DEPS_GIT_BASE="file://$base/org" "$PRZP" add greeter >"$add_out" 2>"$add_err"); then
        printf 'FAIL  deps: przp add failed\n' >&2
        sed -n '1,80p' "$add_err" >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -q '^\[deps\]' "$base/proj/przp.toml" || ! grep -q '^greeter = ' "$base/proj/przp.toml"; then
        printf 'FAIL  deps: przp.toml missing greeter entry after add\n' >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -q '^greeter = ' "$base/proj/przp.lock"; then
        printf 'FAIL  deps: przp.lock missing greeter entry after add\n' >&2
        failures=$((failures + 1))
        return
    fi

    # Delete the fetched checkout so przp run has to exercise ensure_deps's
    # auto-fetch path (missing .przp/deps/) rather than an already-warm one.
    rm -rf "$base/proj/.przp"

    local run_out="$TMP/out/deps_run.stdout"
    local run_err="$TMP/err/deps_run.stderr"
    if ! (cd "$base/proj" && PRZP_STDLIB="$STDLIB" PRZP_DEPS_GIT_BASE="file://$base/org" "$PRZP" run >"$run_out" 2>"$run_err"); then
        printf 'FAIL  deps: przp run (auto-fetch) failed\n' >&2
        sed -n '1,80p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -q '^r=7$' "$run_out"; then
        printf 'FAIL  deps: unexpected przp run output\n' >&2
        cat "$run_out" >&2
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

# OpenGL tests (std/graphics/gl), gated separately on libGL — reuses the
# same Xvfb instance run_x11_setup already started, so it only runs when
# both X11 and GL are available.
run_gl_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local bin="$TMP/bin/$name.gl"
    local actual="$TMP/out/$name.gl.stdout"
    local compile_err="$TMP/err/$name.gl.compile.stderr"
    local run_err="$TMP/err/$name.gl.run.stderr"
    local expected="$src_dir/stdout"

    printf 'gl    %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src_dir/main.przp" -lX11 -lGL -lz -o="$bin" \
            >"$TMP/out/$name.gl.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: gl compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! DISPLAY="$X11_DISPLAY" timeout 10 "$bin" >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: gl run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: gl stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

# run_gl_video_case: run_gl_case plus ffmpeg's libavformat/libavcodec/
# libavutil/libswscale link flags, for the one tests/gl case
# (video_sprite) that also needs std/video — gated separately below on
# top of run_gl_case's own X11/GL gating, since ffmpeg's dev packages
# are a distinct, less commonly pre-installed dependency.
run_gl_video_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local bin="$TMP/bin/$name.gl"
    local actual="$TMP/out/$name.gl.stdout"
    local compile_err="$TMP/err/$name.gl.compile.stderr"
    local run_err="$TMP/err/$name.gl.run.stderr"
    local expected="$src_dir/stdout"

    printf 'gl    %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src_dir/main.przp" -lX11 -lGL -lz \
            -lavformat -lavcodec -lavutil -lswscale -o="$bin" \
            >"$TMP/out/$name.gl.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: gl compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! DISPLAY="$X11_DISPLAY" timeout 10 "$bin" >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: gl run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: gl stdout mismatch\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

# std/video tests (ffmpeg's libavformat/libavcodec/libavutil/libswscale
# FFI). Needs all four libraries' *development* packages at build time
# (not just the runtime .so — see std/video.przp's own doc comment on
# why), gated separately below, skipped (not failed) when absent. Also
# needs -lX11 -lGL -lz: std/video.przp imports std/graphics for
# VideoSprite's GL texture, so even a program that only uses the
# decode-only `Video` type still compiles the whole module (this
# compiler doesn't dead-code-eliminate unused imports) — the same
# reason std/audio's tests need -lasound -lmpg123 even for a program
# that only decodes and never plays anything.
run_video_case() {
    local src="$1"
    local name
    name="$(basename "$src" .przp)"
    local bin="$TMP/bin/$name"
    local actual="$TMP/out/$name.stdout"
    local compile_err="$TMP/err/$name.compile.stderr"
    local run_err="$TMP/err/$name.run.stderr"
    local expected="${src%.przp}.stdout"

    printf 'video %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src" -lX11 -lGL -lz -lavformat -lavcodec -lavutil -lswscale -o="$bin" \
            >"$TMP/out/$name.compile.stdout" 2>"$compile_err"; then
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

# std/zip tests (libzip FFI). Needs libzip-dev's runtime .so, not guaranteed
# on every dev/CI machine — skipped with a message, not failed, when absent.
run_zip_case() {
    local src_dir="$1"
    local name
    name="$(basename "$src_dir")"
    local bin="$TMP/bin/$name.zip"
    local actual="$TMP/out/$name.zip.stdout"
    local compile_err="$TMP/err/$name.zip.compile.stderr"
    local run_err="$TMP/err/$name.zip.run.stderr"
    local run_dir="$TMP/zip/$name"
    local expected="$src_dir/stdout"

    printf 'zip   %s\n' "$name"
    if ! PRZP_STDLIB="$STDLIB" "$PRZP" sac "$src_dir/main.przp" -lzip -o="$bin" \
            >"$TMP/out/$name.zip.compile.stdout" 2>"$compile_err"; then
        printf 'FAIL  %s: zip compile failed\n' "$name" >&2
        sed -n '1,120p' "$compile_err" >&2
        failures=$((failures + 1))
        return
    fi

    rm -rf "$run_dir"
    mkdir -p "$run_dir"
    if [ -d "$src_dir/fixture" ]; then
        cp -r "$src_dir/fixture/." "$run_dir/"
    fi

    if ! (cd "$run_dir" && "$bin") >"$actual" 2>"$run_err"; then
        printf 'FAIL  %s: zip run failed\n' "$name" >&2
        sed -n '1,120p' "$run_err" >&2
        failures=$((failures + 1))
        return
    fi

    if ! diff -u "$expected" "$actual"; then
        printf 'FAIL  %s: zip stdout mismatch\n' "$name" >&2
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

# Regression for stdlib_root_init's binary-relative fallback (no
# $PRZP_STDLIB set): przp must find std/ on its own in both layouts it
# knows how to look for — "std/" sitting right next to the binary (a
# dev checkout running compiler/przp out of compiler/), and
# "../lib/przp/std" (the layout deploy.sh installs). A bare binary with
# std/ deployed nowhere near it (e.g. `cp przp /usr/local/bin/` and
# nothing else) must still fail with a clear "cannot import" error
# rather than silently doing the wrong thing.
run_stdlib_resolution_case() {
    local prog="$TMP/stdlib_resolution/prog.przp"
    mkdir -p "$TMP/stdlib_resolution"
    cat > "$prog" <<'EOF'
import(m = "std/math")
fn main() -> i32 {
    @pf("{m.sqrt(4.0)}\n")
    ret 0
}
EOF

    printf 'res   stdlib_resolution (sibling layout)\n'
    local sib="$TMP/stdlib_resolution/sibling"
    mkdir -p "$sib"
    cp "$PRZP" "$sib/przp"
    cp -r "$STDLIB" "$sib/std"
    if ! out="$(env -u PRZP_STDLIB "$sib/przp" sac "$prog" -o="$sib/prog" 2>"$TMP/err/stdlib_resolution_sibling.stderr" && "$sib/prog")"; then
        printf 'FAIL  stdlib_resolution: sibling layout did not resolve std/\n' >&2
        sed -n '1,60p' "$TMP/err/stdlib_resolution_sibling.stderr" >&2
        failures=$((failures + 1))
    elif [ "$out" != "2.000000" ]; then
        printf 'FAIL  stdlib_resolution: sibling layout produced unexpected output: %s\n' "$out" >&2
        failures=$((failures + 1))
    fi

    printf 'res   stdlib_resolution (deploy.sh FHS layout)\n'
    local fhs="$TMP/stdlib_resolution/fhs"
    mkdir -p "$fhs/bin" "$fhs/lib/przp"
    cp "$PRZP" "$fhs/bin/przp"
    cp -r "$STDLIB" "$fhs/lib/przp/std"
    if ! out="$(env -u PRZP_STDLIB "$fhs/bin/przp" sac "$prog" -o="$fhs/prog" 2>"$TMP/err/stdlib_resolution_fhs.stderr" && "$fhs/prog")"; then
        printf 'FAIL  stdlib_resolution: deploy.sh FHS layout did not resolve std/\n' >&2
        sed -n '1,60p' "$TMP/err/stdlib_resolution_fhs.stderr" >&2
        failures=$((failures + 1))
    elif [ "$out" != "2.000000" ]; then
        printf 'FAIL  stdlib_resolution: FHS layout produced unexpected output: %s\n' "$out" >&2
        failures=$((failures + 1))
    fi

    printf 'res   stdlib_resolution (no std/ deployed)\n'
    local none="$TMP/stdlib_resolution/none"
    mkdir -p "$none"
    cp "$PRZP" "$none/przp"
    if env -u PRZP_STDLIB "$none/przp" sac "$prog" -o="$none/prog" >"$TMP/out/stdlib_resolution_none.stdout" 2>"$TMP/err/stdlib_resolution_none.stderr"; then
        printf 'FAIL  stdlib_resolution: expected a compile failure with no std/ deployed\n' >&2
        failures=$((failures + 1))
    elif ! grep -q "cannot import 'std/math'" "$TMP/err/stdlib_resolution_none.stderr"; then
        printf 'FAIL  stdlib_resolution: wrong error with no std/ deployed\n' >&2
        sed -n '1,60p' "$TMP/err/stdlib_resolution_none.stderr" >&2
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

# `przp run` should skip recompiling when the binary is already newer than
# every source file involved (entry + imports), and rebuild when it isn't —
# on a source edit, an edit to an imported (non-entry) file, or a
# debug/--release mode switch.
run_freshness_case() {
    local work="$TMP/freshness/proj"
    local stdout="$TMP/out/freshness.stdout"
    local stderr="$TMP/err/freshness.stderr"

    printf 'run   run_freshness\n'
    mkdir -p "$work/src"
    cat > "$work/przp.toml" <<'EOF'
[package]
name = "freshness"
version = "0.1.0"

[build]
entry = "src/main.przp"
EOF
    cat > "$work/src/helper.przp" <<'EOF'
fn greet() -> str { ret "v1" }
EOF
    cat > "$work/src/main.przp" <<'EOF'
import(h = "helper")
fn main() -> i32 {
    @pf("{h.greet()}\n")
    ret 0
}
EOF

    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$stdout" 2>"$stderr"); then
        printf 'FAIL  run_freshness: initial run failed\n' >&2
        sed -n '1,120p' "$stderr" >&2
        failures=$((failures + 1))
        return
    fi
    local mtime1
    mtime1=$(stat -c '%Y' "$work/freshness")

    sleep 1.1
    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$stdout" 2>"$stderr"); then
        printf 'FAIL  run_freshness: second run failed\n' >&2
        failures=$((failures + 1))
        return
    fi
    local mtime2
    mtime2=$(stat -c '%Y' "$work/freshness")
    if [ "$mtime1" != "$mtime2" ]; then
        printf 'FAIL  run_freshness: unchanged sources triggered a rebuild\n' >&2
        failures=$((failures + 1))
        return
    fi

    sleep 1.1
    cat > "$work/src/helper.przp" <<'EOF'
fn greet() -> str { ret "v2" }
EOF
    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$stdout" 2>"$stderr"); then
        printf 'FAIL  run_freshness: run after import edit failed\n' >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -Fq "v2" "$stdout"; then
        printf 'FAIL  run_freshness: editing an imported file did not trigger a rebuild\n' >&2
        sed -n '1,20p' "$stdout" >&2
        failures=$((failures + 1))
        return
    fi

    sleep 1.1
    local mtime3
    mtime3=$(stat -c '%Y' "$work/freshness")
    if ! (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run --release >"$stdout" 2>"$stderr"); then
        printf 'FAIL  run_freshness: --release run failed\n' >&2
        failures=$((failures + 1))
        return
    fi
    local mtime4
    mtime4=$(stat -c '%Y' "$work/freshness")
    if [ "$mtime3" = "$mtime4" ]; then
        printf 'FAIL  run_freshness: switching to --release did not force a rebuild\n' >&2
        failures=$((failures + 1))
    fi
}

# `przp test` — test "name" { ... } blocks, discovered across the entry file
# and an imported module, run in isolated per-test subprocesses (so a
# crashing test can't take any other test down with it), with przp test /
# przp test <file> / przp test <file> <name> filtering.
run_test_cmd_case() {
    local work="$TMP/test_cmd/proj"
    local stdout="$TMP/out/test_cmd.stdout"
    local stderr="$TMP/err/test_cmd.stderr"

    printf 'run   test_cmd\n'
    mkdir -p "$work/src"
    cat > "$work/przp.toml" <<'EOF'
[package]
name = "test_cmd"
version = "0.1.0"
EOF
    cat > "$work/src/helper.przp" <<'EOF'
fn divide(a: i32, b: i32) -> i32 { ret a / b }

test "helper division works" {
    @assert(divide(10, 2) == 5)
}

test "helper crashes on purpose" {
    x: *i32 = null
    @pf("{x.*}\n")
}
EOF
    cat > "$work/src/main.przp" <<'EOF'
import(h = "helper")

fn add(a: i32, b: i32) -> i32 { ret a + b }

test "addition works" {
    @assert(add(2, 3) == 5)
    @pass()
}

test "addition is wrong on purpose" {
    @assert(add(2, 3) == 999)
}

fn main() -> i32 {
    @pf("real program\n")
    ret 0
}
EOF

    # all tests: 2 pass, 2 fail (one via @assert, one via a null-deref crash)
    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" test >"$stdout" 2>"$stderr")
    local rc=$?
    if [ "$rc" -ne 1 ]; then
        printf 'FAIL  test_cmd: expected exit 1 with 2 failing tests, got %d\n' "$rc" >&2
        sed -n '1,40p' "$stdout" "$stderr" >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -Fq "2 passed, 2 failed" "$stdout"; then
        printf 'FAIL  test_cmd: expected "2 passed, 2 failed" summary\n' >&2
        sed -n '1,40p' "$stdout" >&2
        failures=$((failures + 1))
        return
    fi

    # file filter: only helper.przp's two tests
    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" test src/helper.przp >"$stdout" 2>"$stderr")
    if ! grep -Fq "1 passed, 1 failed" "$stdout"; then
        printf 'FAIL  test_cmd: file filter did not select exactly helper.przp'"'"'s 2 tests\n' >&2
        sed -n '1,40p' "$stdout" >&2
        failures=$((failures + 1))
        return
    fi

    # file + name filter: exactly one passing test
    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" test src/main.przp "addition works" >"$stdout" 2>"$stderr")
    rc=$?
    if [ "$rc" -ne 0 ] || ! grep -Fq "1 passed, 0 failed" "$stdout"; then
        printf 'FAIL  test_cmd: file+name filter did not select exactly one passing test\n' >&2
        sed -n '1,40p' "$stdout" >&2
        failures=$((failures + 1))
        return
    fi

    # przp run's own main is unaffected by the presence of test blocks
    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$stdout" 2>"$stderr")
    if ! grep -Fq "real program" "$stdout"; then
        printf 'FAIL  test_cmd: przp run did not execute the real fn main\n' >&2
        sed -n '1,40p' "$stdout" >&2
        failures=$((failures + 1))
    fi
}

# `przp test` discovers tests/*.przp on its own, independent of what
# src/main.przp imports; its build artifacts land under tests/, and a stale
# artifact from a previous/renamed build gets swept away without touching
# a *.przp source file whose name happens to contain "_test".
run_tests_dir_case() {
    local work="$TMP/tests_dir/proj"
    local stdout="$TMP/out/tests_dir.stdout"
    local stderr="$TMP/err/tests_dir.stderr"

    printf 'run   tests_dir\n'
    mkdir -p "$work/src" "$work/tests"
    cat > "$work/przp.toml" <<'EOF'
[package]
name = "tdir"
version = "0.1.0"
EOF
    cat > "$work/src/main.przp" <<'EOF'
fn main() -> i32 {
    @pf("real program\n")
    ret 0
}
EOF
    # not imported by src/main.przp anywhere — discovery must not depend on imports
    cat > "$work/tests/standalone_test.przp" <<'EOF'
test "standalone discovered" {
    @assert(1 + 1 == 2)
}
EOF
    # a legitimate source file whose name itself contains "_test"
    cat > "$work/tests/login_test.przp" <<'EOF'
test "login source survives cleanup" {
    @pass()
}
EOF
    # simulate leftover artifacts from a prior/renamed build
    touch "$work/tests/oldpkg_test" "$work/tests/oldpkg_test.tests" "$work/tests/oldpkg_test.d"

    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" test >"$stdout" 2>"$stderr")
    local rc=$?
    if [ "$rc" -ne 0 ] || ! grep -Fq "2 passed, 0 failed" "$stdout"; then
        printf 'FAIL  tests_dir: expected both tests/ files discovered and passing\n' >&2
        sed -n '1,40p' "$stdout" "$stderr" >&2
        failures=$((failures + 1))
        return
    fi

    if [ ! -x "$work/tests/tdir_test" ] || [ ! -f "$work/tests/tdir_test.tests" ]; then
        printf 'FAIL  tests_dir: compiled test binary/manifest not placed under tests/\n' >&2
        failures=$((failures + 1))
        return
    fi
    if [ -e "$work/tests/oldpkg_test" ] || [ -e "$work/tests/oldpkg_test.tests" ] || [ -e "$work/tests/oldpkg_test.d" ]; then
        printf 'FAIL  tests_dir: stale oldpkg_test artifacts were not cleaned up\n' >&2
        failures=$((failures + 1))
        return
    fi
    if [ ! -f "$work/tests/login_test.przp" ]; then
        printf 'FAIL  tests_dir: cleanup deleted a *.przp source file named like an artifact\n' >&2
        failures=$((failures + 1))
        return
    fi
}

# regression: a tests/ file importing the same generic module the entry file
# also imports (each under its own alias) used to fail — merge_items() only
# merged items, never gen_insts, so a tests/ file's own qualified generic
# usage (recorded as a gen_inst against its own parser) never reached sema
# once merged into the entry file's module.
run_tests_dir_generic_case() {
    local work="$TMP/tests_dir_generic/proj"
    local stdout="$TMP/out/tests_dir_generic.stdout"
    local stderr="$TMP/err/tests_dir_generic.stderr"

    printf 'run   tests_dir_generic\n'
    mkdir -p "$work/src" "$work/tests"
    cat > "$work/przp.toml" <<'EOF'
[package]
name = "tdgen"
version = "0.1.0"
EOF
    cat > "$work/src/logic.przp" <<'EOF'
struct Box<T> { value: T }
impl Box<T> {
    fn new(v: T) -> Box<T> { ret Box<T>{.value=v} }
    fn get(self: *Box<T>) -> T { ret self.value }
}
EOF
    cat > "$work/src/main.przp" <<'EOF'
import(logic = "logic")
fn main() -> i32 {
    b: logic.Box<i32> = logic.Box<i32>.new(42)
    @pf("{b.get()}\n")
    ret 0
}
EOF
    cat > "$work/tests/box_test.przp" <<'EOF'
import(t = "../src/logic")
test "box via a different alias than main.przp uses" {
    b: t.Box<i32> = t.Box<i32>.new(1)
    @assert(b.get() == 1)
}
EOF

    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$stdout" 2>"$stderr")
    if [ "$?" -ne 0 ] || ! grep -Fq "42" "$stdout"; then
        printf 'FAIL  tests_dir_generic: przp run did not print 42\n' >&2
        sed -n '1,40p' "$stdout" "$stderr" >&2
        failures=$((failures + 1))
        return
    fi

    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" test >"$stdout" 2>"$stderr")
    if [ "$?" -ne 0 ] || ! grep -Fq "1 passed, 0 failed" "$stdout"; then
        printf 'FAIL  tests_dir_generic: przp test did not pass\n' >&2
        sed -n '1,40p' "$stdout" "$stderr" >&2
        failures=$((failures + 1))
    fi
}

# regression: przp test used to fail with "redefinition of ..." whenever the
# entry file and a tests/ file both imported the same (non-generic) stdlib
# module under the same alias — merge_items() concatenated each independent
# top-level file's own mangled copy of that module with no de-duplication
# by resolved path, so every one of that module's symbols ended up defined
# twice. Both src/main.przp and tests/*.przp import std/str as `str` here,
# an extremely ordinary thing for a real project to do.
run_tests_dir_dedup_case() {
    local work="$TMP/tests_dir_dedup/proj"
    local stdout="$TMP/out/tests_dir_dedup.stdout"
    local stderr="$TMP/err/tests_dir_dedup.stderr"

    printf 'run   tests_dir_dedup\n'
    mkdir -p "$work/src" "$work/tests"
    cat > "$work/przp.toml" <<'EOF'
[package]
name = "tddedup"
version = "0.1.0"
EOF
    cat > "$work/src/main.przp" <<'EOF'
import(str = "std/str")
fn main() -> i32 {
    @pf("{str.to_upper(\"hello\")}\n")
    ret 0
}
EOF
    cat > "$work/tests/str_test.przp" <<'EOF'
import(str = "std/str")
test "same module, same alias, as the entry file's own direct import" {
    @assert(str.to_upper("hi") == "HI")
}
EOF

    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" run >"$stdout" 2>"$stderr")
    if [ "$?" -ne 0 ] || ! grep -Fq "HELLO" "$stdout"; then
        printf 'FAIL  tests_dir_dedup: przp run did not print HELLO\n' >&2
        sed -n '1,40p' "$stdout" "$stderr" >&2
        failures=$((failures + 1))
        return
    fi

    (cd "$work" && PRZP_STDLIB="$STDLIB" "$PRZP" test >"$stdout" 2>"$stderr")
    if [ "$?" -ne 0 ] || ! grep -Fq "1 passed, 0 failed" "$stdout"; then
        printf 'FAIL  tests_dir_dedup: przp test did not pass\n' >&2
        sed -n '1,40p' "$stdout" "$stderr" >&2
        failures=$((failures + 1))
    fi
}

for src in "$ROOT"/tests/run/*.przp; do
    [ -e "$src" ] || continue
    # std_image_gif needs -lz (std/image's uncompress() extern) — run via
    # run_lz_case below instead of the plain no-extra-links case here.
    case "$(basename "$src")" in
        std_image_gif.przp|std_audio.przp|std_audio_control.przp|std_video_decode.przp) continue ;;
    esac
    run_success_case "$src"
done

if ldconfig -p 2>/dev/null | grep -q "libz\.so"; then
    run_lz_case "$ROOT/tests/run/std_image_gif.przp"
else
    printf 'skip  std_image_gif: libz not installed\n'
fi

if ldconfig -p 2>/dev/null | grep -q "libasound\.so" && ldconfig -p 2>/dev/null | grep -q "libmpg123\.so"; then
    run_audio_case "$ROOT/tests/run/std_audio.przp"
    run_audio_case "$ROOT/tests/run/std_audio_control.przp"
else
    printf 'skip  std_audio: libasound/libmpg123 not installed\n'
fi

HAVE_FFMPEG=0
if ldconfig -p 2>/dev/null | grep -q "libavformat\.so" \
        && ldconfig -p 2>/dev/null | grep -q "libavcodec\.so" \
        && ldconfig -p 2>/dev/null | grep -q "libavutil\.so" \
        && ldconfig -p 2>/dev/null | grep -q "libswscale\.so" \
        && ldconfig -p 2>/dev/null | grep -q "libX11\.so" \
        && ldconfig -p 2>/dev/null | grep -q "libGL\.so" \
        && ldconfig -p 2>/dev/null | grep -q "libz\.so"; then
    HAVE_FFMPEG=1
    run_video_case "$ROOT/tests/run/std_video_decode.przp"
else
    printf 'skip  std_video_decode: libavformat/libavcodec/libavutil/libswscale (or libX11/libGL/libz) not installed\n'
fi

for dir in "$ROOT"/tests/project/*; do
    [ -d "$dir" ] || continue
    run_project_case "$dir"
done

for dir in "$ROOT"/tests/project-fail/*; do
    [ -d "$dir" ] || continue
    run_project_fail_case "$dir"
done

run_deps_case

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
    if ldconfig -p 2>/dev/null | grep -q "libGL\.so"; then
        for dir in "$ROOT"/tests/gl/*; do
            [ -d "$dir" ] || continue
            [ "$(basename "$dir")" = "video_sprite" ] && continue
            run_gl_case "$dir"
        done
        if [ "$HAVE_FFMPEG" = "1" ]; then
            run_gl_video_case "$ROOT/tests/gl/video_sprite"
        else
            printf 'skip  gl video_sprite: libavformat/libavcodec/libavutil/libswscale not installed\n'
        fi
    else
        printf 'skip  gl tests: libGL not installed\n'
    fi
    run_x11_teardown
fi

if ldconfig -p 2>/dev/null | grep -q "libzip\.so"; then
    for dir in "$ROOT"/tests/zip/*; do
        [ -d "$dir" ] || continue
        run_zip_case "$dir"
    done
else
    printf 'skip  zip tests: libzip not installed\n'
fi

run_stdlib_resolution_case
run_init_case
run_freshness_case
run_test_cmd_case
run_tests_dir_case
run_tests_dir_generic_case
run_tests_dir_dedup_case

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
