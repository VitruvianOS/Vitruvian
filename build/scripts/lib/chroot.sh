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
    sudo rm -f "$_chroot_dir/usr/bin/$_qemu_name" \
        "$_chroot_dir/usr/bin/${_qemu_name%-static}" 2>/dev/null || true
}

# Debian mirror used by debootstrap and apt inside the chroot. Override
# by exporting DEBIAN_MIRROR before invoking setupenv / bake.
: "${DEBIAN_MIRROR:=http://deb.debian.org/debian/}"

# Debian suite bootstrapped for the chroot and every board rootfs; trixie
# is the only suite proven against so far.
: "${VOS_BASE_SUITE:=trixie}"

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

# chroot_isolated ROOT CMD [ARGS...]
# Runs CMD chrooted into ROOT with its own network, hostname, IPC, PID and mount namespaces and a
# read-only /proc/sys and /sys, so maintainer scripts cannot touch the host. /dev stays the host's.
_CHROOT_ISOLATED_INNER='r="$1"; shift
ip link set lo up 2>/dev/null
mount -t proc proc "$r/proc" || exit 1
mount --bind "$r/proc/sys" "$r/proc/sys" || exit 1
mount -o remount,bind,ro "$r/proc/sys" || exit 1
if mountpoint -q "$r/sys"; then umount -R "$r/sys" || exit 1; fi
mount -t sysfs -o ro sysfs "$r/sys" || exit 1
exec chroot "$r" "$@"'

chroot_isolated() {
    _ci_root="$1"
    shift
    sudo mkdir -p "$_ci_root/proc" "$_ci_root/sys"
    sudo unshare --net --uts --ipc --pid --fork --mount --propagation private \
        /bin/sh -c "$_CHROOT_ISOLATED_INNER" sh "$_ci_root" "$@"
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
        # Keep only the most recent backup — older ones balloon disk usage.
        for _stale in "$_chroot_dir".old-*; do
            [ -e "$_stale" ] || continue
            log_info "Removing stale chroot backup: $_stale"
            sudo rm -rf "$_stale"
        done
        sudo mv "$_chroot_dir" "$_backup"
    fi

    mkdir -p "$_basedir/image_tree"

    _cache_dir="$(chroot_cache_dir "$_basedir")"
    _debootstrap_cache="$_cache_dir/debootstrap"
    sudo mkdir -p "$_debootstrap_cache" "$_cache_dir/archives/partial"
    log_info "Using package cache: $_cache_dir (mirror: $DEBIAN_MIRROR)"

    if is_cross_build "$_arch"; then
        log_step "Bootstrapping Debian $VOS_BASE_SUITE ($_deb_arch) [foreign]..."
        sudo debootstrap --arch="$_deb_arch" --variant=minbase --foreign \
            --cache-dir="$_debootstrap_cache" \
            "$VOS_BASE_SUITE" "$_chroot_dir" "$DEBIAN_MIRROR"
        qemu_inject "$_chroot_dir" "$_arch"
    else
        log_step "Bootstrapping Debian $VOS_BASE_SUITE ($_deb_arch)..."
        sudo debootstrap --arch="$_deb_arch" --variant=minbase \
            --cache-dir="$_debootstrap_cache" \
            "$VOS_BASE_SUITE" "$_chroot_dir" "$DEBIAN_MIRROR"
    fi

    trap 'chroot_umount "$_chroot_dir"' EXIT
    chroot_mount "$_chroot_dir"
    chroot_mount_deb_cache "$_chroot_dir" "$_cache_dir"

    # Force apt inside the chroot onto the same mirror so its sources.list
    # matches what debootstrap used. debootstrap writes a default one
    # pointing at the mirror, but make it explicit and overridable.
    : "${DEBIAN_SECURITY_MIRROR:=http://security.debian.org/debian-security}"
    sudo tee "$_chroot_dir/etc/apt/sources.list" >/dev/null <<EOF
deb $DEBIAN_MIRROR $VOS_BASE_SUITE main contrib non-free non-free-firmware
deb $DEBIAN_MIRROR $VOS_BASE_SUITE-updates main contrib non-free non-free-firmware
deb $DEBIAN_SECURITY_MIRROR $VOS_BASE_SUITE-security main contrib non-free non-free-firmware
EOF

    # Written only when a key is supplied: an unverifiable repo fails the
    # whole apt update, taking Debian access down with it.
    : "${VOS_REPO_URL:=https://repo.v-os.dev}"
    if [ -n "${VOS_REPO_KEY:-}" ] && [ -f "$VOS_REPO_KEY" ]; then
        # No default suite on purpose. The retired trixie-testing sat here
        # for months and kept resolving to a suite that no longer exists;
        # with four suites now, defaulting to any one of them is the same
        # bug one rename later.
        [ -n "${VOS_REPO_SUITE:-}" ] || die "VOS_REPO_SUITE is unset and a repo key was supplied; name the suite to install from (trixie, testing, trixie-nightly, testing-nightly)"
        sudo install -d -m 755 "$_chroot_dir/etc/apt/keyrings"
        sudo install -m 644 "$VOS_REPO_KEY" \
            "$_chroot_dir/etc/apt/keyrings/vitruvian-archive-keyring.asc"
        sudo install -d -m 755 "$_chroot_dir/etc/apt/sources.list.d"
        sudo tee "$_chroot_dir/etc/apt/sources.list.d/vitruvian.sources" >/dev/null <<VOSEOF
Types: deb
URIs: $VOS_REPO_URL
Suites: $VOS_REPO_SUITE
Components: main
Signed-By: /etc/apt/keyrings/vitruvian-archive-keyring.asc
VOSEOF
        log_info "VitruvianOS repo enabled: $VOS_REPO_URL $VOS_REPO_SUITE"
    else
        log_warn "VOS_REPO_KEY unset or missing; image will NOT see the VitruvianOS repo"
    fi

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
        # The second stage unpacks from /var/cache/apt/archives, which is
        # bound to $_cache_dir/archives; debootstrap cached elsewhere.
        sudo sh -c 'cp -n "$1"/*.deb "$2"/ 2>/dev/null || true' _ \
            "$_debootstrap_cache" "$_cache_dir/archives"
        log_step "Running debootstrap second stage..."
        chroot_isolated "$_chroot_dir" /debootstrap/debootstrap --second-stage
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
    # dpkg's per-file fsyncs are pure overhead on a chroot that gets
    # discarded; opt-in so it is never silently on for a local tree.
    if [ "${VOS_UNSAFE_IO:-0}" = 1 ]; then
        sudo install -d -m 755 "$_chroot_dir/etc/dpkg/dpkg.cfg.d"
        printf 'force-unsafe-io\n' \
          | sudo tee "$_chroot_dir/etc/dpkg/dpkg.cfg.d/vos-build-unsafe-io" >/dev/null
    fi
    # Download with the network, install isolated: cups-pdf's postinst ran lpadmin against the host's cupsd.
    sudo chroot "$_chroot_dir" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "\
echo 'vitruvian' > /etc/hostname && \
apt update && apt install -y --download-only --no-install-recommends $_base_pkgs $_dev_pkgs \$DEBUG_PACKAGES"
    chroot_isolated "$_chroot_dir" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "\
apt install -y --no-install-recommends $_base_pkgs $_dev_pkgs \$DEBUG_PACKAGES && \
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
    # Keep only the most recent backup.
    for _stale in "$_chroot_dir".old-*; do
        [ -e "$_stale" ] || continue
        log_info "Removing stale chroot backup: $_stale"
        sudo rm -rf "$_stale"
    done
    sudo mv "$_chroot_dir" "$_backup"

    chroot_create "$_basedir" "$_arch"
}
