#!/bin/sh

BOLD=$(tput bold 2>/dev/null || printf '')
NORMAL=$(tput sgr0 2>/dev/null || printf '')

log_step() {
    printf '%s%s%s\n' "$BOLD" "$1" "$NORMAL"
}

log_info() {
    printf '[+] %s\n' "$1"
}

log_warn() {
    printf '[!] %s\n' "$1" >&2
}

log_error() {
    printf '[-] %s\n' "$1" >&2
}

die() {
    log_error "$1"
    exit 1
}

arch_to_deb() {
    case "$1" in
        amd64)   printf 'amd64' ;;
        arm64)   printf 'arm64' ;;
        arm32)   printf 'armhf' ;;
        riscv64) printf 'riscv64' ;;
        *)       die "Unknown architecture: $1" ;;
    esac
}

arch_to_qemu() {
    case "$1" in
        amd64)   printf 'qemu-system-x86_64' ;;
        arm64)   printf 'qemu-system-aarch64' ;;
        arm32)   printf 'qemu-system-arm' ;;
        riscv64) printf 'qemu-system-riscv64' ;;
        *)       die "No qemu mapping for: $1" ;;
    esac
}

arch_to_deb_host() {
    case "$1" in
        amd64)   printf 'x86_64-linux-gnu' ;;
        arm64)   printf 'aarch64-linux-gnu' ;;
        arm32)   printf 'arm-linux-gnueabihf' ;;
        riscv64) printf 'riscv64-linux-gnu' ;;
        *)       die "Unknown deb host triple for: $1" ;;
    esac
}

arch_to_efi_target() {
    case "$1" in
        amd64)   printf 'x86_64-efi' ;;
        arm64)   printf 'arm64-efi' ;;
        arm32)   printf '' ;;
        riscv64) printf 'riscv64-efi' ;;
        *)       die "No EFI target for: $1" ;;
    esac
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1 (install $2)"
}

host_arch() {
    _harch="$(uname -m)"
    case "$_harch" in
        x86_64)  printf 'amd64' ;;
        aarch64) printf 'arm64' ;;
        armv7l)  printf 'arm32' ;;
        riscv64) printf 'riscv64' ;;
        *)       printf '%s' "$_harch" ;;
    esac
}

is_cross_build() {
    [ "$1" != "$(host_arch)" ]
}

arch_to_qemu_user() {
    case "$1" in
        amd64)   printf 'qemu-x86_64-static' ;;
        arm64)   printf 'qemu-aarch64-static' ;;
        arm32)   printf 'qemu-arm-static' ;;
        riscv64) printf 'qemu-riscv64-static' ;;
        *)       die "No qemu-user mapping for: $1" ;;
    esac
}

find_qemu_user_binary() {
    _qemu="$(arch_to_qemu_user "$1")"
    for _path in "/usr/bin/$_qemu" "/usr/local/bin/$_qemu"; do
        if [ -f "$_path" ]; then
            printf '%s' "$_path"
            return 0
        fi
    done
    die "$_qemu not found. Install qemu-user-static for $1 cross-build support."
}

register_binfmt() {
    if [ ! -f /proc/sys/fs/binfmt_misc/status ]; then
        log_warn "binfmt_misc not mounted. Mounting..."
        sudo mount binfmt_misc -t binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true
    fi
}


