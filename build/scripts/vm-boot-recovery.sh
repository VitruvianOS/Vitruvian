#!/bin/sh
# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
# Set GRUB next_entry on a raw image ESP for unattended V\OS safe-mode boot.

set -eu

# Must match the menuentry title image.sh embeds in the raw-image grub.cfg.
RECOVERY_ENTRY="Vitruvian (Safe Mode)"
ESP_MOUNTPOINT=""

usage() {
    echo "Usage: $0 <image.raw>" >&2
    exit 1
}

cleanup() {
    if [ -n "$ESP_MOUNTPOINT" ] && mountpoint -q "$ESP_MOUNTPOINT" 2>/dev/null; then
        sudo umount "$ESP_MOUNTPOINT" 2>/dev/null || sudo umount -l "$ESP_MOUNTPOINT" 2>/dev/null || true
    fi
    if [ -n "${_loop:-}" ]; then
        sudo losetup -d "$_loop" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

[ $# -ge 1 ] || usage
_IMAGE="$1"

if [ ! -f "$_IMAGE" ]; then
    echo "Error: image file not found: $_IMAGE" >&2
    exit 1
fi

_loop=$(sudo losetup --show -f -P "$_IMAGE")
ESP_DEVICE="${_loop}p1"

if [ ! -b "$ESP_DEVICE" ]; then
    echo "Error: partition 1 not found on $_loop" >&2
    exit 1
fi

ESP_MOUNTPOINT=$(mktemp -d)
# Writable: grub-editenv set cannot update the block on a read-only mount.
sudo mount "$ESP_DEVICE" "$ESP_MOUNTPOINT"

GRUBENV="$ESP_MOUNTPOINT/boot/grub/grubenv"

if [ ! -f "$GRUBENV" ]; then
    # Older images shipped without one; create a block so set can run.
    sudo mkdir -p "$(dirname "$GRUBENV")"
    sudo grub-editenv "$GRUBENV" create 2>/dev/null \
        || printf '# GRUB Environment Block\n' | sudo tee "$GRUBENV" >/dev/null
fi

sudo grub-editenv "$GRUBENV" set "next_entry=$RECOVERY_ENTRY"
echo "grubenv: next_entry set to '$RECOVERY_ENTRY'"

sudo umount "$ESP_MOUNTPOINT"
ESP_MOUNTPOINT=""
sudo losetup -d "$_loop"
_loop=""

echo "Ready: $_IMAGE will boot into $RECOVERY_ENTRY on next start."
