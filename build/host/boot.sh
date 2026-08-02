#!/usr/bin/env bash
#
# Boots a built Vitruvian ISO in QEMU/KVM. Needs no root
# (just membership in the `kvm` group).
#
# Usage:
#     bash build/host/boot.sh                          # graphical QEMU window
#     bash build/host/boot.sh --enable-console-stdout  # + serial console to terminal
#
# Optional environment variables:
#     ARCH=amd64   target architecture (default amd64 -> generated.amd64)
#
set -euo pipefail

# Derive the repo path from the script's own location (build/host/ -> repo).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
ARCH="${ARCH:-amd64}"
GEN="$REPO/generated.$ARCH"

if [ ! -e /dev/kvm ]; then
	echo "!! /dev/kvm does not exist - KVM is not available." >&2
	exit 1
fi

if [ ! -d "$GEN" ]; then
	echo "!! Could not find $GEN - build the ISO first: bash build/host/build-iso.sh" >&2
	exit 1
fi

cd "$GEN"
echo "==> Booting Vitruvian in QEMU (close the QEMU window to quit)..."
exec ../bake boot --image-type=iso "$@"
