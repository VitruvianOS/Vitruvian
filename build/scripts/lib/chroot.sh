#!/bin/sh

qemu_inject() {
    _chroot_dir="$1"
    _target_arch="$2"
    if ! is_cross_build "$_target_arch"; then
        return 0
    fi
    _qemu_bin="$(find_qemu_user_binary "$_target_arch")"
    _qemu_name="$(basename "$_qemu_bin")"
    log_info "Cross-build detected: injecting $_qemu_name into chroot"
    sudo cp "$_qemu_bin" "$_chroot_dir/usr/bin/$_qemu_name"
    sudo chmod +x "$_chroot_dir/usr/bin/$_qemu_name"
    register_binfmt
}

qemu_eject() {
    _chroot_dir="$1"
    _target_arch="$2"
    if ! is_cross_build "$_target_arch"; then
        return 0
    fi
    _qemu_name="$(arch_to_qemu_user "$_target_arch")"
    sudo rm -f "$_chroot_dir/usr/bin/$_qemu_name" 2>/dev/null || true
}

# Debian mirror used by debootstrap and apt inside the chroot. Override
# by exporting DEBIAN_MIRROR before invoking setupenv / bake.
: "${DEBIAN_MIRROR:=http://deb.debian.org/debian/}"

# Persistent .deb cache shared across chroot regenerations. Path is per
# arch (laid down by setupenv); same arch == same cache.
chroot_cache_dir() {
    echo "$1/deb"
}

chroot_mount() {
    _chroot_dir="$1"
    [ -d "$_chroot_dir" ] || die "Chroot directory not found: $_chroot_dir"

    sudo mkdir -p "$_chroot_dir/proc" "$_chroot_dir/sys" "$_chroot_dir/dev"

    if ! mountpoint -q "$_chroot_dir/proc" 2>/dev/null; then
        sudo mount -t proc / "$_chroot_dir/proc"
    fi
    if ! mountpoint -q "$_chroot_dir/sys" 2>/dev/null; then
        sudo mount --rbind /sys "$_chroot_dir/sys"
        sudo mount --make-rslave "$_chroot_dir/sys"
    fi
    if ! mountpoint -q "$_chroot_dir/dev" 2>/dev/null; then
        sudo mount --rbind /dev "$_chroot_dir/dev"
        sudo mount --make-rslave "$_chroot_dir/dev"
    fi

    # Provision DNS so `apt update` inside the chroot can reach the mirrors.
    # -L dereferences the host's systemd-resolved stub symlink.
    sudo mkdir -p "$_chroot_dir/etc"
    sudo cp -L /etc/resolv.conf "$_chroot_dir/etc/resolv.conf"
}

chroot_umount() {
    _chroot_dir="$1"
    [ -d "$_chroot_dir" ] || return 0

    sudo umount -l "$_chroot_dir/var/cache/apt/archives" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/proc" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/sys" 2>/dev/null || true
    #sudo umount -l "$_chroot_dir/dev/pts" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/dev" 2>/dev/null || true
}

# Bind the persistent .deb cache into the chroot. Idempotent.
chroot_mount_deb_cache() {
    _chroot_dir="$1"
    _cache_dir="$2"
    sudo mkdir -p "$_cache_dir/archives/partial"
    sudo mkdir -p "$_chroot_dir/var/cache/apt/archives"
    if ! mountpoint -q "$_chroot_dir/var/cache/apt/archives" 2>/dev/null; then
        sudo mount --bind "$_cache_dir/archives" \
            "$_chroot_dir/var/cache/apt/archives"
    fi
    # Make apt keep what it downloads (default config drops .debs after
    # install, defeating the cache). Per-config-file drop so apt-get
    # update / install honor it without touching the chroot's own conf.
    sudo mkdir -p "$_chroot_dir/etc/apt/apt.conf.d"
    echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' \
        | sudo tee "$_chroot_dir/etc/apt/apt.conf.d/99-keep-debs" >/dev/null
}

chroot_create() {
    _basedir="$1"
    _arch="$2"
    _deb_arch="$(arch_to_deb "$_arch")"
    _chroot_dir="$_basedir/image_tree/chroot"

    if [ -d "$_chroot_dir" ]; then
        _ts=$(date +%Y%m%d-%H%M%S)
        _backup="$_chroot_dir.old-$_ts"
        log_info "Found existing chroot, moving to $_backup"
        chroot_umount "$_chroot_dir"
        sudo mv "$_chroot_dir" "$_backup"
    fi

    mkdir -p "$_basedir/image_tree"

    _cache_dir="$(chroot_cache_dir "$_basedir")"
    _debootstrap_cache="$_cache_dir/debootstrap"
    sudo mkdir -p "$_debootstrap_cache" "$_cache_dir/archives/partial"
    log_info "Using package cache: $_cache_dir (mirror: $DEBIAN_MIRROR)"

    if is_cross_build "$_arch"; then
        log_step "Bootstrapping Debian trixie ($_deb_arch) [foreign]..."
        sudo debootstrap --arch="$_deb_arch" --variant=minbase --foreign \
            --cache-dir="$_debootstrap_cache" \
            trixie "$_chroot_dir" "$DEBIAN_MIRROR"
        qemu_inject "$_chroot_dir" "$_arch"
    else
        log_step "Bootstrapping Debian trixie ($_deb_arch)..."
        sudo debootstrap --arch="$_deb_arch" --variant=minbase \
            --cache-dir="$_debootstrap_cache" \
            trixie "$_chroot_dir" "$DEBIAN_MIRROR"
    fi

    trap 'chroot_umount "$_chroot_dir"' EXIT
    chroot_mount "$_chroot_dir"
    chroot_mount_deb_cache "$_chroot_dir" "$_cache_dir"

    # Force apt inside the chroot onto the same mirror so its sources.list
    # matches what debootstrap used. debootstrap writes a default one
    # pointing at the mirror, but make it explicit and overridable.
    : "${DEBIAN_SECURITY_MIRROR:=http://security.debian.org/debian-security}"
    sudo tee "$_chroot_dir/etc/apt/sources.list" >/dev/null <<EOF
deb $DEBIAN_MIRROR trixie main contrib non-free non-free-firmware
deb $DEBIAN_MIRROR trixie-updates main contrib non-free non-free-firmware
deb $DEBIAN_SECURITY_MIRROR trixie-security main contrib non-free non-free-firmware
EOF

    log_step "Verifying mount points before second-stage..."
    log_info "Checking proc: mountpoint=$(mountpoint -q "$_chroot_dir/proc" 2>/dev/null && echo yes || echo no), stat=$([ -f "$_chroot_dir/proc/1/stat" ] && echo exists || echo missing)"
    if mountpoint -q "$_chroot_dir/proc" 2>/dev/null; then
        log_info "proc mounted"
    elif [ -f "$_chroot_dir/proc/1/stat" ]; then
        log_info "proc accessible"
    else
        die "proc mount failed - cannot proceed with second-stage"
    fi
    mountpoint -q "$_chroot_dir/sys" || die "sys mount failed"
    mountpoint -q "$_chroot_dir/dev" || die "dev mount failed"

    if is_cross_build "$_arch"; then
        log_step "Running debootstrap second stage..."
        sudo chroot "$_chroot_dir" /debootstrap/debootstrap --second-stage
        log_step "Re-mounting after second-stage..."
        chroot_mount "$_chroot_dir"
    fi

    log_step "Verifying mount points after second-stage..."
    if mountpoint -q "$_chroot_dir/proc" 2>/dev/null; then
        log_info "proc mounted"
    elif [ -f "$_chroot_dir/proc/1/stat" ]; then
        log_info "proc accessible"
    else
        log_warn "proc mount may have failed - continuing anyway"
    fi

    log_step "Creating essential directories..."
    sudo mkdir -p "$_chroot_dir/dev"
    sudo mkdir -p "$_chroot_dir/proc"
    sudo mkdir -p "$_chroot_dir/sys"
    sudo mkdir -p "$_chroot_dir/run"
    sudo mkdir -p "$_chroot_dir/tmp"
    for _d in dev/hugepages dev/mqueue run/lock \
              sys/kernel/debug sys/kernel/tracing \
              sys/fs/fuse/connections sys/kernel/config; do
        sudo mkdir -p "$_chroot_dir/$_d"
    done

    _base_pkgs="$(get_base_packages "$_arch")"
    _dev_pkgs="$(get_dev_packages "$_arch")"

    log_step "Installing packages..."
    sudo chroot "$_chroot_dir" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "\
echo 'vitruvian' > /etc/hostname && \
apt update && apt install -y --no-install-recommends $_base_pkgs $_dev_pkgs \$DEBUG_PACKAGES && \
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen && locale-gen && \
exit"

    ls "$_chroot_dir/lib/modules" | head -n1 > "$_basedir/imagekernelversion.conf"

    qemu_eject "$_chroot_dir" "$_arch"
    chroot_umount "$_chroot_dir"

    log_info "Chroot created at $_chroot_dir"
}

chroot_regenerate() {
    _basedir="$1"
    _arch="$2"
    _chroot_dir="$_basedir/image_tree/chroot"

    if [ ! -d "$_chroot_dir" ]; then
        log_info "No existing chroot at $_chroot_dir, creating fresh."
        chroot_create "$_basedir" "$_arch"
        return
    fi

    log_step "Regenerating chroot..."
    chroot_umount "$_chroot_dir"

    _ts=$(date +%Y%m%d-%H%M%S)
    _backup="$_chroot_dir.old-$_ts"
    sudo mv "$_chroot_dir" "$_backup"

    chroot_create "$_basedir" "$_arch"
}
