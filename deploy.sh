#!/usr/bin/env bash
# deploy.sh — build and install the przp compiler + standard library.
#
# Installing just the przp binary (e.g. `sudo cp przp /usr/local/bin/`)
# is not enough on its own: przp resolves std/* imports against a
# directory it finds relative to its own binary path (or $PRZP_STDLIB,
# if set), and a bare binary with no std/ anywhere near it fails every
# `import(x = "std/...")` with "cannot import 'std/...': No such file or
# directory". This script installs both, in the layout przp already
# knows how to find on its own:
#
#   $PREFIX/bin/przp          the compiler binary
#   $PREFIX/lib/przp/std/     the standard library it imports against
#
# Usage:
#   ./deploy.sh                 # install to /usr/local (needs sudo/root)
#   sudo ./deploy.sh
#   PREFIX="$HOME/.local" ./deploy.sh   # user-local install, no sudo
#   ./deploy.sh --uninstall
#   ./deploy.sh --no-build      # skip `make`, install whatever's already built
#   ./deploy.sh --install-deps  # also install missing optional system libs (Linux/apt)
#   ./deploy.sh --skip-deps-check   # skip the optional-library check entirely
#
# Optional system library check: przp itself only needs `clang` to
# build. But a *program* that imports std/graphics (window+GL), std/
# image (PNG/GIF), std/audio (MP3+ALSA), or std/video (MP4 via ffmpeg)
# needs the corresponding development package installed at link time —
# the runtime .so isn't enough; the linker needs the unversioned .so
# symlink only a -dev package installs. Missing one produces a
# confusing "undefined reference" error from deep inside a linker
# invocation, nowhere near its real cause (see book/graphics.md's
# std/audio and std/video sections for the exact packages each needs),
# so this script checks for all of them after installing przp itself
# and either reports what's missing or, with --install-deps, installs
# it (apt-based Linux only for now — other package managers get the
# missing package names printed instead of an automatic install, since
# their exact package names aren't verified here).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
LIBDIR="$PREFIX/lib/przp"

do_build=1
do_uninstall=0
do_install_deps=0
do_skip_deps_check=0

for arg in "$@"; do
    case "$arg" in
        --no-build)         do_build=0 ;;
        --uninstall)         do_uninstall=1 ;;
        --install-deps)      do_install_deps=1 ;;
        --skip-deps-check)   do_skip_deps_check=1 ;;
        -h|--help)
            sed -n '2,37p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "deploy.sh: unknown argument '$arg'" >&2
            exit 1
            ;;
    esac
done

# has_lib: is the linker-visible, unversioned .so symlink for $1 present?
# (the same check tests/run.sh's own library-gated test categories use)
# — the runtime .so.N alone isn't enough for `przp sac`/`build` to link
# against it, only a -dev package's symlink is. Collects ldconfig's
# output into a variable and matches against that (rather than piping
# straight into `grep -q`) deliberately: under `set -o pipefail`,
# `grep -q` exiting the moment it finds a match can SIGPIPE `ldconfig`
# before it finishes writing, and pipefail then reports the whole
# pipeline as failed even though the match was real — a real race that
# intermittently misreported an installed library as missing.
has_lib() {
    local cache
    cache="$(ldconfig -p 2>/dev/null || true)"
    [[ "$cache" == *"$1.so"* ]]
}

# check_deps: report which optional, feature-gated system libraries are
# present/missing, grouped by which std/ module needs them. Every
# group's absence just means a program that imports that module won't
# link — przp itself, and everything not touching graphics/audio/video,
# is entirely unaffected.
check_deps() {
    if [ "$(uname -s)" != "Linux" ]; then
        echo "Skipping optional system library check (only implemented for Linux)."
        return 0
    fi

    echo
    echo "Checking optional system libraries (only needed to build a program"
    echo "that imports the corresponding std/ module — not przp itself):"

    DEPS_MISSING_APT=()
    local group libs apt_pkgs lib missing_libs

    for group in \
        "std/graphics,std/image|libX11 libGL libz|libx11-dev libgl-dev zlib1g-dev" \
        "std/audio|libasound libmpg123|libasound2-dev libmpg123-dev" \
        "std/video|libavformat libavcodec libavutil libswscale|libavformat-dev libavcodec-dev libavutil-dev libswscale-dev"
    do
        IFS='|' read -r modules libs apt_pkgs <<< "$group"
        missing_libs=()
        for lib in $libs; do
            has_lib "$lib" || missing_libs+=("$lib")
        done
        if [ ${#missing_libs[@]} -eq 0 ]; then
            printf '  [ok]      %-13s %s\n' "$modules" "($libs)"
        else
            printf '  [missing] %-13s missing: %s\n' "$modules" "${missing_libs[*]}"
            local pkg_arr
            IFS=' ' read -ra pkg_arr <<< "$apt_pkgs"
            DEPS_MISSING_APT+=("${pkg_arr[@]}")
        fi
    done
    echo

    if [ ${#DEPS_MISSING_APT[@]} -eq 0 ]; then
        echo "All optional libraries present."
        return 0
    fi

    if [ "$do_install_deps" -eq 1 ]; then
        if command -v apt-get >/dev/null 2>&1; then
            echo "Installing missing packages: ${DEPS_MISSING_APT[*]}"
            apt-get update && apt-get install -y "${DEPS_MISSING_APT[@]}"
        else
            echo "deploy.sh: --install-deps only knows how to drive apt-get, and it's not on PATH here."
            echo "Install these packages yourself with your system's package manager:"
            echo "  ${DEPS_MISSING_APT[*]}"
        fi
    else
        echo "Some std/ modules won't link until their libraries are installed. Either:"
        echo "  sudo apt install ${DEPS_MISSING_APT[*]}"
        echo "or re-run this script with --install-deps to have it do that for you."
    fi
}

if [ "$do_uninstall" -eq 1 ]; then
    echo "Removing $BINDIR/przp and $LIBDIR ..."
    rm -f "$BINDIR/przp"
    rm -rf "$LIBDIR"
    echo "Uninstalled."
    exit 0
fi

if [ "$do_build" -eq 1 ]; then
    echo "Building compiler..."
    make -C "$REPO_ROOT/compiler"
fi

if [ ! -x "$REPO_ROOT/compiler/przp" ]; then
    echo "deploy.sh: $REPO_ROOT/compiler/przp not found or not executable — build it first (or drop --no-build)" >&2
    exit 1
fi

echo "Installing to $PREFIX ..."
mkdir -p "$BINDIR" "$LIBDIR"
cp "$REPO_ROOT/compiler/przp" "$BINDIR/przp"
rm -rf "$LIBDIR/std"
cp -r "$REPO_ROOT/compiler/std" "$LIBDIR/std"

echo
echo "Installed:"
echo "  $BINDIR/przp"
echo "  $LIBDIR/std/"
echo

if ! command -v przp >/dev/null 2>&1 || [ "$(command -v przp)" != "$BINDIR/przp" ]; then
    echo "Note: $BINDIR is not first on your PATH (or not on it at all)."
    echo "Add it, e.g.:"
    echo "  export PATH=\"$BINDIR:\$PATH\""
    echo
fi

if [ "$do_skip_deps_check" -eq 0 ]; then
    check_deps
fi

echo "przp finds std/ next to its own binary automatically — no \$PRZP_STDLIB needed."
echo "Verify with:"
echo "  echo 'fn main() -> i32 { @pf(\"it works\\n\") ret 0 }' > /tmp/przp_check.przp"
echo "  \"$BINDIR/przp\" sac /tmp/przp_check.przp -o=/tmp/przp_check && /tmp/przp_check"
