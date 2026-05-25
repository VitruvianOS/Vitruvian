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

}

chroot_umount() {
    _chroot_dir="$1"
    [ -d "$_chroot_dir" ] || return 0

    sudo umount -l "$_chroot_dir/proc" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/sys" 2>/dev/null || true
    #sudo umount -l "$_chroot_dir/dev/pts" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/dev" 2>/dev/null || true
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

    if is_cross_build "$_arch"; then
        log_step "Bootstrapping Debian trixie ($_deb_arch) [foreign]..."
        sudo debootstrap --arch="$_deb_arch" --variant=minbase --foreign \
            trixie "$_chroot_dir" http://deb.debian.org/debian/
        qemu_inject "$_chroot_dir" "$_arch"
    else
        log_step "Bootstrapping Debian trixie ($_deb_arch)..."
        sudo debootstrap --arch="$_deb_arch" --variant=minbase \
            trixie "$_chroot_dir" http://deb.debian.org/debian/
    fi

    trap 'chroot_umount "$_chroot_dir"' EXIT
    chroot_mount "$_chroot_dir"

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
