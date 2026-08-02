#!/usr/bin/env bash
#
# Installs all build dependencies for Vitruvian on Ubuntu (tested on 26.04).
#
# Two sources of packages:
#   1. A curated BASE list of host toolchain / image / QEMU packages that the
#      build & image scripts need (cmake, ninja, debootstrap, xorriso, qemu, ...).
#   2. The library -dev packages from the configure-generated build_deps.txt,
#      if the tree has already been configured. Reading that file keeps this
#      script from going stale when build/deps.cmake changes.
#
# Also handles the kisak-mesa PPA conflict: if libgbm1 comes from kisak, the
# archive -dev packages (libgbm-dev, ...) won't install without the PPA present.
#
# Run as root:   sudo bash build/host/install-deps.sh
#
# Optional environment variables:
#     ARCH=amd64   target architecture (default amd64); selects the QEMU system
#                  emulator and the build_deps.txt under generated.$ARCH/
#
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
	echo "!! Run as root:  sudo bash $0" >&2
	exit 1
fi

# Derive the repo path from the script's own location (build/host/ -> repo).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
ARCH="${ARCH:-amd64}"
GEN="$REPO/generated.$ARCH"

# --- Curated host packages (toolchain / image / boot) ------------------------
# Not covered by build_deps.txt, which lists only library -dev deps.
BASE_TOOLCHAIN="build-essential cmake ninja-build pkg-config git flex bison gettext dpkg-dev rsync"
# Image assembly: debootstrap (chroot), squashfs, ISO (xorriso+grub+mtools),
# filesystems (vfat/ext4/xfs), partitioning. libbfd-dev is what build-iso.sh
# would otherwise install on its own.
BASE_IMAGE="debootstrap squashfs-tools xorriso mtools dosfstools e2fsprogs xfsprogs parted grub-common grub-pc-bin grub-efi-amd64-bin libbfd-dev"

# QEMU emulator matched to the target arch (boot.sh / bake boot).
case "$ARCH" in
	amd64|x86_64)  BASE_QEMU="qemu-system-x86 qemu-utils ovmf" ;;
	arm64)         BASE_QEMU="qemu-system-arm qemu-utils qemu-efi-aarch64" ;;
	arm32)         BASE_QEMU="qemu-system-arm qemu-utils" ;;
	riscv64)       BASE_QEMU="qemu-system-misc qemu-utils" ;;
	*)             BASE_QEMU="qemu-utils" ;;
esac

BASE="$BASE_TOOLCHAIN $BASE_IMAGE $BASE_QEMU"

# --- Library -dev deps from the configured tree (authoritative, if present) ---
LIBDEPS=""
if [ -f "$GEN/build_deps.txt" ]; then
	# build_deps.txt is a single line, tokens separated by spaces and/or ';'
	# (CMake list joins). Normalise both to whitespace.
	LIBDEPS="$(tr ';' ' ' < "$GEN/build_deps.txt" | tr -s '[:space:]' ' ')"
	echo "==> Using library deps from $GEN/build_deps.txt"
else
	echo "==> $GEN/build_deps.txt not found (tree not configured yet)."
	echo "    Installing BASE only; re-run after ./configure to pull library -dev deps,"
	echo "    or install them per build/deps.cmake / wiki.w-os.dev."
fi

ALL="$BASE $LIBDEPS"

# --- kisak-mesa PPA (version match for the installed libgbm1) -----------------
echo "==> Checking the kisak-mesa PPA (to match the installed libgbm1 version)..."
if grep -rqi "kisak" /etc/apt/sources.list.d/ 2>/dev/null; then
	echo "    kisak PPA already present in sources."
else
	echo "    Adding ppa:kisak/kisak-mesa ..."
	apt-get install -y software-properties-common
	if ! add-apt-repository -y ppa:kisak/kisak-mesa; then
		cat >&2 <<'EOF'

!! Adding the kisak PPA failed (it may not have a branch for your Ubuntu release yet).
   Two options:
     1) Wait until kisak publishes a branch for your release, then re-run this script.
     2) Revert Mesa to the archive version (WARNING: downgrades the whole Mesa stack):
          sudo apt install ppa-purge
          sudo ppa-purge ppa:kisak/kisak-mesa
        then re-run this script.
EOF
		exit 1
	fi
fi

echo "==> apt update ..."
apt-get update

echo "==> Installing dependencies ..."
# shellcheck disable=SC2086
apt-get install -y $ALL

echo
echo "==> Verifying:"
missing=""
for p in $ALL; do
	if dpkg -s "$p" 2>/dev/null | grep -q "install ok installed"; then
		printf "    OK      %s\n" "$p"
	else
		printf "    MISSING %s\n" "$p"
		missing="$missing $p"
	fi
done

echo
if [ -n "$missing" ]; then
	echo "!! Some packages did not install:$missing" >&2
	exit 1
fi
echo "[+] Done - all dependencies are installed."
echo "    Next:  bash build/host/build-iso.sh"
