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

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
LIBDIR="$PREFIX/lib/przp"

do_build=1
do_uninstall=0

for arg in "$@"; do
    case "$arg" in
        --no-build)   do_build=0 ;;
        --uninstall)  do_uninstall=1 ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "deploy.sh: unknown argument '$arg'" >&2
            exit 1
            ;;
    esac
done

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

echo "przp finds std/ next to its own binary automatically — no \$PRZP_STDLIB needed."
echo "Verify with:"
echo "  echo 'fn main() -> i32 { @pf(\"it works\\n\") ret 0 }' > /tmp/przp_check.przp"
echo "  \"$BINDIR/przp\" sac /tmp/przp_check.przp -o=/tmp/przp_check && /tmp/przp_check"
