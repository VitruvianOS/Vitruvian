#!/usr/bin/env bash
#
# Builds a bootable Vitruvian ISO (chroot + image) from a non-Debian host
# (tested on Ubuntu 26.04 / glibc 2.43 / gcc 15). The host compiles against a
# Debian-trixie chroot, so the result runs regardless of the host's lib versions.
#
# Run as a NORMAL user (NOT under sudo!):
#     bash build/host/build-iso.sh
#
# The script requests sudo itself, only where needed (apt install, debootstrap
# inside bake). Do not run it under sudo - the output files would end up owned
# by root.
#
# Optional environment variables:
#     ARCH=amd64   target architecture (default amd64 -> generated.amd64)
#
set -euo pipefail

# Derive the repo path from the script's own location (build/host/ -> repo) so
# this works for anyone after `git clone`, not just on one machine.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
ARCH="${ARCH:-amd64}"
GEN="$REPO/generated.$ARCH"

if [ "$(id -u)" -eq 0 ]; then
	echo "!! Do not run as root/sudo. Run as a normal user:" >&2
	echo "     bash $0" >&2
	echo "   (the script requests sudo itself for apt and debootstrap.)" >&2
	exit 1
fi

if [ ! -d "$GEN" ]; then
	echo "!! Could not find $GEN - has the tree been configured yet?" >&2
	echo "   Configure the build tree first (see wiki.w-os.dev)." >&2
	exit 1
fi

echo "==> (1/4) Installing the last build dependency (libbfd-dev)..."
sudo apt install -y libbfd-dev

cd "$GEN"

echo "==> (2/4) Creating chroot (debootstrap - downloads ~hundreds of MB, a few minutes)..."
../configure --chroot-build --buildtools=../buildtools

echo "==> (3/4) Baking the ISO image (bake build --image-type=iso)..."
../bake build --image-type=iso

echo "==> (4/4) Locating the resulting ISO..."
iso="$(find "$GEN/image_tree" "$GEN/output" -maxdepth 2 -name '*.iso' 2>/dev/null | head -1 || true)"
if [ -n "$iso" ]; then
	ls -lh "$iso"
	echo
	echo "[+] Done. Boot it in QEMU:   bash build/host/boot.sh"
else
	echo "!! ISO not found - check the bake output above." >&2
	exit 1
fi
