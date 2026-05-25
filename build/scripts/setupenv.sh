#!/bin/sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIB_DIR="$SCRIPT_DIR/lib"

. "$LIB_DIR/common.sh"
. "$LIB_DIR/packages.sh"
. "$LIB_DIR/chroot.sh"

CHROOT_BUILD=0
ARCH=""

usage() {
    cat <<'EOF'
Usage: setupenv.sh [options]

Options:
  --chroot-build    Create the build chroot at image_tree/chroot
  --arch=ARCH       Target architecture: amd64, arm64, arm32, riscv64 (default: amd64)
  --help            Show this help

Without --chroot-build, only writes ARCH to buildstate.conf (no chroot created).
NOTE: image-producing builds (bake build --image-type=iso/raw) require the
chroot, so --chroot-build is needed even when compiling natively. Only skip
it if you do not intend to produce an image from this build tree.
EOF
    exit 0
}

for arg in "$@"; do
    case "$arg" in
        --chroot-build)
            CHROOT_BUILD=1
            ;;
        --arch=*)
            ARCH="${arg#*=}"
            ;;
        --help|-h)
            usage
            ;;
        *)
            die "Unknown option: $arg"
            ;;
    esac
done

ARCH="${ARCH:-amd64}"

case "$ARCH" in
    amd64|arm64|arm32|riscv64) ;;
    *) die "Unsupported architecture: $ARCH (use amd64, arm64, arm32, riscv64)" ;;
esac

BASEDIR="$(realpath ./)"

if [ -f "$BASEDIR/buildstate.conf" ]; then
    _prev_arch="$(. "$BASEDIR/buildstate.conf" && printf '%s' "${ARCH:-}")"
    if [ -n "$_prev_arch" ] && [ "$_prev_arch" != "$ARCH" ]; then
        die "This directory was set up for $_prev_arch but you requested $ARCH. Remove the generated directory and start fresh, or rerun with --arch=$_prev_arch."
    fi
fi

printf 'ARCH=%s\nCHROOT_BUILD=%s\n' "$ARCH" "$CHROOT_BUILD" > "$BASEDIR/buildstate.conf"

if [ "$CHROOT_BUILD" -eq 1 ]; then
    log_step "Setting up chroot build environment ($ARCH)..."
    chroot_create "$BASEDIR" "$ARCH"
else
    log_step "Native build selected ($ARCH). No chroot created."
    log_info "buildstate.conf written with ARCH=$ARCH"
fi

log_info "Setup complete."
