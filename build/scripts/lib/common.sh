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

# sudo resets the environment; chroot apt sees a proxy only as an env argument.
CHROOT_PROXY="${http_proxy:+http_proxy=$http_proxy}"

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

# native_pkg_unpack <chroot-dir>: split the package phase so unpack stops
# running under qemu. apt inside the chroot has already
# downloaded the full dependency closure into var/cache/apt/archives (and any
# project debs sit in <chroot>/localdeb); this unpacks them with the HOST's
# native dpkg. dpkg --root runs maintainer scripts chrooted into the target,
# so preinst/postinst stay genuinely-guest code (binfmt picks them up) -- the
# win is the decompression/extraction bulk, which needs no target execution.
#
# The chroot must already have /proc /dev /sys mounted: preinsts run at
# unpack time. Version-skew guard: host and chroot dpkg must share a
# major.minor (same Debian suite); a mismatch is refused loudly rather than
# writing a dpkg db the guest cannot read.
native_pkg_unpack() {
    _npu_root="${1:?native_pkg_unpack: chroot dir required}"
    [ -d "$_npu_root/var/lib/dpkg" ] \
        || { log_error "native unpack: no dpkg db at $_npu_root/var/lib/dpkg"; return 1; }

    _npu_host="$(dpkg-query -W -f='${Version}' dpkg 2>/dev/null || echo none)"
    _npu_guest="$(dpkg-query --admindir="$_npu_root/var/lib/dpkg" -W -f='${Version}' dpkg 2>/dev/null || echo none)"
    _npu_hm="$(printf '%s' "$_npu_host"   | sed 's/-.*//; s/[.][^.]*$//')"
    _npu_gm="$(printf '%s' "$_npu_guest" | sed 's/-.*//; s/[.][^.]*$//')"
    if [ "$_npu_host" = none ] || [ "$_npu_guest" = none ] || [ "$_npu_hm" != "$_npu_gm" ]; then
        log_error "native unpack refused: host dpkg $_npu_host vs chroot dpkg $_npu_guest (major.minor must match)"
        return 1
    fi

    _npu_debs=""
    for _npu_d in "$_npu_root"/var/cache/apt/archives/*.deb; do
        [ -f "$_npu_d" ] && _npu_debs="$_npu_debs $_npu_d"
    done
    # Project debs last: nexus-dkms' preinst interlock needs the kernel
    # headers' files present, which unpacking the archives has created.
    for _npu_d in "$_npu_root"/localdeb/*.deb; do
        [ -f "$_npu_d" ] && _npu_debs="$_npu_debs $_npu_d"
    done
    if [ -z "$_npu_debs" ]; then
        log_error "native unpack: no debs under $_npu_root/var/cache/apt/archives or localdeb"
        return 1
    fi

    log_step "Native unpack: $(echo $_npu_debs | wc -w) debs via host dpkg $_npu_host (no qemu)"
    # --force-depends: dpkg enforces Pre-Depends at UNPACK time as
    # *configured* state, which a single bulk unpack pass can never provide
    # (systemd pre-depends libsystemd-shared "unpacked but never configured",
    # python3-minimal pre-depends python3.13-minimal, etc.). The preinsts that
    # run during unpack need the dependency's FILES, which the glob order
    # (archives before localdebs, alphabetical within) does provide. The
    # in-chroot `dpkg --configure -a` that follows configures everything in
    # proper dependency order — that step is where configured-state matters.
    # shellcheck disable=SC2086  # word-splitting the deb list is intended
    sudo dpkg --root="$_npu_root" --unpack --force-depends $_npu_debs
}

# Debian leaves the sbin dirs off a normal user's PATH, but sfdisk, mke2fs
# and mkfs.vfat live there.
case ":$PATH:" in
    *:/usr/sbin:*) ;;
    *) PATH="$PATH:/usr/sbin:/sbin"; export PATH ;;
esac

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

# Debian forky's qemu-user ships the static binaries without the -static
# suffix (qemu-aarch64), so accept both names.
find_qemu_user_binary() {
    _qemu="$(arch_to_qemu_user "$1")"
    for _path in "/usr/bin/$_qemu" "/usr/local/bin/$_qemu" \
                 "/usr/bin/${_qemu%-static}" "/usr/local/bin/${_qemu%-static}"; do
        if [ -f "$_path" ]; then
            printf '%s' "$_path"
            return 0
        fi
    done
    die "$_qemu not found. Install qemu-user-static (qemu-user on forky) for $1 cross-build support."
}

register_binfmt() {
    if [ ! -f /proc/sys/fs/binfmt_misc/status ]; then
        log_warn "binfmt_misc not mounted. Mounting..."
        sudo mount binfmt_misc -t binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true
    fi
}

# Write $1/MANIFEST: sha256 + size for every file in $1.
#   $1  artifact directory   $2  architecture (for the header)
generate_manifest() {
    _mdir="$1"
    _march="${2:-unknown}"
    [ -d "$_mdir" ] || { log_warn "generate_manifest: directory not found: $_mdir"; return 1; }

    _manifest="$_mdir/MANIFEST"
    log_step "Generating MANIFEST in $_mdir ..."

    {
        printf '# MANIFEST generated by bake at %s  arch=%s\n' \
            "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$_march"
        printf '# sha256  size  filename\n'
        for _f in "$_mdir"/*; do
            [ -f "$_f" ] || continue
            _base="$(basename "$_f")"
            [ "$_base" = "MANIFEST" ] && continue
            _sum="$(sha256sum "$_f" | cut -d' ' -f1)"
            _size="$(stat -c%s "$_f" 2>/dev/null || wc -c < "$_f")"
            printf '%s  %s  %s\n' "$_sum" "$_size" "$_base"
        done
    } > "$_manifest"

    log_info "MANIFEST written: $_manifest ($(wc -l < "$_manifest") lines)"
}


