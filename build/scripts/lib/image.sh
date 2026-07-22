#!/bin/sh

# Shared cleanup for all loop-device image builders (create_raw, create_raspberry, create_uboot_board).
# Reads globals: _mnt, _loop, _raw — set before the trap is armed.
# Safe to call multiple times (success path + trap). Synchronous: waits for
# lazy umounts to actually release the loop, retries losetup -d, and as a
# last resort detaches every loop still backed by $_raw.
_loop_image_cleanup() {
    # Unmount everything beneath $_mnt, deepest first, so lazy umounts on
    # parents don't get blocked by mounted children.
    if [ -n "$_mnt" ]; then
        for _mp in $(awk -v m="$_mnt" '$2 ~ "^"m {print $2}' /proc/mounts | sort -r); do
            sudo umount "$_mp" 2>/dev/null \
                || sudo umount -l "$_mp" 2>/dev/null || true
        done
    fi

    sudo udevadm settle 2>/dev/null || true

    # Retry losetup -d a few times — lazy umounts release the bdev
    # asynchronously, so an immediate detach can hit EBUSY.
    if [ -n "$_loop" ]; then
        _i=0
        while [ $_i -lt 10 ]; do
            sudo losetup -d "$_loop" 2>/dev/null && break
            _i=$((_i + 1))
            sleep 0.2
        done
    fi

    # Belt-and-braces: if anything is still attached to $_raw (subshells,
    # udisks auto-mounts, partition children), detach them all.
    if [ -n "$_raw" ] && [ -f "$_raw" ]; then
        for _stale in $(sudo losetup -j "$_raw" -O NAME --noheadings 2>/dev/null); do
            for _mp in $(awk -v dev="$_stale" '$1 ~ dev {print $2}' /proc/mounts \
                    | sort -r); do
                sudo umount "$_mp" 2>/dev/null \
                    || sudo umount -l "$_mp" 2>/dev/null || true
            done
            sudo losetup -d "$_stale" 2>/dev/null || true
        done
    fi
}

_iso_cleanup() {
    sudo umount -l "$_chroot_dir/var/cache/apt/archives" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/proc/" 2>/dev/null || true
    sudo umount -l "$_chroot_dir/tmp/"  2>/dev/null || true
}

create_raw() {
    _basedir="$1"
    _arch="$2"
    _efi_target="$(arch_to_efi_target "$_arch")"
    if [ -z "$_efi_target" ]; then
        die "EFI raw images not supported on $_arch. Use a board-specific image type instead."
    fi

    _chroot_dir="$_basedir/image_tree/chroot"
    [ -d "$_chroot_dir" ] || die "No chroot at $_chroot_dir. Run setupenv first."

    _imagekernelversion=$(cat "$_basedir/imagekernelversion.conf" 2>/dev/null \
        || die "imagekernelversion.conf not found. Run setupenv first.")

    _count=$(ls -1 "$_basedir"/*.deb 2>/dev/null | wc -l)
    if [ "$_count" -eq 0 ]; then
        die "No deb files generated! Run cpack inside $_basedir/"
    fi

    require_cmd rsync rsync

    _raw="$_basedir/output/vitruvian.raw"
    _mnt="/mnt/vitruvian"
    _hostname="vitruvian"
    _user=""
    _pass=""
    _ovmf_vars="$_basedir/OVMF_VARS.fd"
    _host_shared="$_basedir/shared"
    _guest_mnt="/mnt/host_shared"

    mkdir -p "$_basedir/output"
    mkdir -p "$_host_shared"

    # Detach any loop devices that previous runs left attached to the same
    # raw file (kill -9 / closed terminal bypassing the EXIT trap, or
    # udisks held it open). Writing through a fresh qemu-img create while
    # a stale loop is still open silently corrupts the new FS.
    if [ -f "$_raw" ]; then
        for _stale in $(sudo losetup -j "$_raw" -O NAME --noheadings 2>/dev/null); do
            log_warn "Detaching stale loop device $_stale"
            # Unmount everything attached to this loop, including udisks's
            # /run/media mounts. Sort reverse so children unmount first.
            for _mp in $(awk -v dev="$_stale" '$1 ~ dev {print $2}' /proc/mounts \
                    | sort -r); do
                sudo umount -l "$_mp" 2>/dev/null || true
            done
            sudo losetup -d "$_stale" 2>/dev/null || true
        done
    fi
    sudo umount -l "$_mnt/boot/efi" 2>/dev/null || true
    sudo umount -l "$_mnt"          2>/dev/null || true

    log_step "Creating RAW image..."
    qemu-img create "$_raw" 4G

    _loop=$(sudo losetup --show -f -P "$_raw")
    log_info "Loop device: $_loop"
    trap '_loop_image_cleanup' EXIT INT TERM

    sudo parted --script "$_loop" mklabel gpt
    sudo parted --script "$_loop" mkpart ESP fat32 1MiB 513MiB
    sudo parted --script "$_loop" set 1 esp on
    sudo parted --script "$_loop" mkpart primary ext4 513MiB 100%
    sudo partprobe "$_loop"
    sudo udevadm settle

    _efi_part="${_loop}p1"
    _root_part="${_loop}p2"

    sudo mkfs.vfat -F32 "$_efi_part"
    # -I 512: more inline xattr headroom (~350 bytes vs ~100 with default
    # 256-byte inodes), so BeOS-style small attrs land inline.
    # ea_inode is intentionally NOT enabled — GRUB 2.12 can't read it, and
    # our attr usage stays under the 4KB shared-block ceiling.
    # ^orphan_file / ^metadata_csum_seed / ^casefold / ^encrypt / ^verity:
    # features GRUB 2.12 doesn't handle. All exclusions must live in ONE
    # -O argument or Debian's mke2fs.conf re-enables them.
    sudo mkfs.ext4 -F -I 512 \
        -O ^orphan_file,^metadata_csum_seed,^casefold,^encrypt,^verity \
        -L vitruvian-root "$_root_part"

    _esp_uuid=$(sudo blkid -s UUID -o value "$_efi_part")
    _root_uuid=$(sudo blkid -s UUID -o value "$_root_part")
    [ -n "$_esp_uuid" ]  || die "Could not read ESP UUID from $_efi_part"
    [ -n "$_root_uuid" ] || die "Could not read root UUID from $_root_part"

    sudo mkdir -p "$_mnt"
    sudo mount "$_root_part" "$_mnt"
    sudo mkdir -p "$_mnt/boot/efi"
    sudo mount "$_efi_part" "$_mnt/boot/efi"

    log_step "Copying chroot into RAW image (rsync)..."
    sudo rsync -aHAXx --numeric-ids \
        --exclude='/proc/*' --exclude='/sys/*' --exclude='/dev/*' \
        --exclude='/tmp/*'  --exclude='/run/*'  --exclude='/localdeb' \
        --exclude='/scratch' \
        "$_chroot_dir/" "$_mnt/"

    sudo mkdir -p "$_mnt/proc" "$_mnt/sys" "$_mnt/dev" "$_mnt/run" "$_mnt/tmp"
    sudo mount -t proc proc "$_mnt/proc"
    sudo mount --rbind /sys "$_mnt/sys";  sudo mount --make-rslave "$_mnt/sys"
    sudo mount --rbind /dev "$_mnt/dev";  sudo mount --make-rslave "$_mnt/dev"
    sudo cp -L /etc/resolv.conf "$_mnt/etc/resolv.conf"

    qemu_inject "$_mnt" "$_arch"

    chroot_mount_deb_cache "$_mnt" "$(chroot_cache_dir "$_basedir")"

    sudo mkdir -p "$_mnt/localdeb"
    sudo cp "$_basedir"/*.deb "$_mnt/localdeb/"

    log_step "Configuring system, installing Vitruvian, and setting up bootloader..."

    _raw_pkgs="$(get_raw_image_packages "$_arch")"
    sudo chroot "$_mnt" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "set -e

# Hide host EFI variables from any package post-install that might call
# efibootmgr — without this, the rbound /sys exposes host NVRAM.
umount /sys/firmware/efi/efivars 2>/dev/null || true

apt-get remove -y vos nexus-dkms 2>/dev/null || true

# live-boot comes in via prior ISO builds rsync'd into this chroot. apt-get
# purge runs update-initramfs through dpkg triggers, which executes the
# live hook BEFORE the package file is unlinked — so purge itself trips
# the hook and aborts. Strip the hook/script files first, then force-purge
# so dpkg state matches the filesystem and no later kernel postinst
# resurrects the failure.
rm -f /usr/share/initramfs-tools/hooks/live*
rm -f /usr/share/initramfs-tools/scripts/live*
rm -f /etc/initramfs-tools/conf.d/live*
dpkg --purge --force-all live-boot live-boot-initramfs-tools \
    live-config live-config-systemd live-tools 2>/dev/null || true
apt-get -y autoremove --purge 2>/dev/null || true

apt-get install -y --no-install-recommends $_raw_pkgs

# Re-derive the running kernel version from /lib/modules. linux-image
# metapackages can land a newer ABI than imagekernelversion.conf knew about.
_kver=\$(ls -1 /lib/modules | sort -V | tail -1)
apt-get install -y --no-install-recommends dkms build-essential \"linux-headers-\$_kver\"

# dpkg -i is expected to fail when local debs pull deps the chroot doesn't
# yet have — apt-get install -f resolves them on the next line.
dpkg -i /localdeb/*.deb || true
apt-get install -f -y --no-install-recommends

depmod -v \"\$_kver\"

# Ensure /vmlinuz and /initrd.img point at the installed kernel. linux-base
# normally drops these via dpkg triggers, but when chroot apt activity is
# weird (e.g. install order, dpkg triggers not fully processed) the
# symlinks can be missing — and the standalone EFI bootloader resolves
# (\$root)/vmlinuz, so a missing symlink means \"you need to load the kernel
# first\" at the GRUB prompt.
ln -sfn boot/vmlinuz-\$_kver /vmlinuz
ln -sfn boot/initrd.img-\$_kver /initrd.img

# Write /etc/default/grub from scratch — grub-common's postinst doesn't
# always populate it in a fresh chroot, and we own every setting here.
mkdir -p /etc/default
cat > /etc/default/grub <<'GRUBEOF'
GRUB_DEFAULT=0
GRUB_TIMEOUT=0
GRUB_DISTRIBUTOR=\"Vitruvian\"
GRUB_CMDLINE_LINUX_DEFAULT=\"quiet splash loglevel=3 systemd.show_status=0 rd.udev.log_priority=3\"
GRUB_CMDLINE_LINUX=\"\"
GRUB_DISABLE_OS_PROBER=true
GRUBEOF

# No grub-install — the EFI loader is built by the host via grub-mkstandalone
# after the chroot exits (same approach as the ISO). update-grub is still run
# so /boot/grub/grub.cfg exists for users who later want to edit it.
# /boot/grub is normally created by grub-pc / grub-efi-amd64 postinst, which
# we don't install (the -bin packages alone don't create it).
mkdir -p /boot/grub
update-grub

cat > /etc/fstab <<FSTABEOF
UUID=$_root_uuid /            ext4   defaults,relatime,errors=remount-ro  0 1
UUID=$_esp_uuid  /boot/efi    vfat   umask=0077,shortname=mixed  0 2
host_shared      $_guest_mnt  9p     trans=virtio,version=9p2000.L,rw,nofail,x-systemd.device-timeout=1s 0 0
FSTABEOF

mkdir -p $_guest_mnt
rm -rf /localdeb" || die "raw chroot bash-c failed"

    _common_chroot_setup "$_mnt" "$_hostname" "$_user" "$_pass" \
        || die "_common_chroot_setup failed"

    case "$_arch" in
        amd64)   _boot_efi="BOOTX64.EFI" ;;
        arm64)   _boot_efi="BOOTAA64.EFI" ;;
        riscv64) _boot_efi="BOOTRISCV64.EFI" ;;
        i386)    _boot_efi="BOOTIA32.EFI" ;;
    esac

    log_step "Building standalone EFI bootloader ($_efi_target)..."
    mkdir -p "$_basedir/image_tree/scratch"
    cat > "$_basedir/image_tree/scratch/raw_embedded_grub.cfg" <<EOF
insmod part_gpt
insmod fat
insmod ext2
insmod search
insmod search_fs_uuid
insmod linux
insmod normal
insmod all_video
insmod gfxterm
set timeout=1
search --no-floppy --fs-uuid --set=root $_root_uuid
menuentry "Vitruvian" {
    linux (\$root)/vmlinuz root=UUID=$_root_uuid rw quiet splash loglevel=3 systemd.show_status=false rd.udev.log_priority=3 console=ttyS0,115200 earlyprintk=ttyS0,115200 ignore_loglevel
    initrd (\$root)/initrd.img
}
EOF

    case "$_arch" in
        amd64)
            require_cmd grub-mkstandalone grub-common
            grub-mkstandalone \
                --format="$_efi_target" \
                --output="$_basedir/image_tree/scratch/$_boot_efi" \
                --locales="" --fonts="" \
                "boot/grub/grub.cfg=$_basedir/image_tree/scratch/raw_embedded_grub.cfg"
            if [ -d /usr/lib/grub/i386-efi ]; then
                grub-mkstandalone \
                    --format=i386-efi \
                    --output="$_basedir/image_tree/scratch/BOOTIA32.EFI" \
                    --locales="" --fonts="" \
                    "boot/grub/grub.cfg=$_basedir/image_tree/scratch/raw_embedded_grub.cfg"
            fi
            ;;
        arm64|riscv64)
            sudo mkdir -p "$_mnt/scratch"
            sudo cp "$_basedir/image_tree/scratch/raw_embedded_grub.cfg" "$_mnt/scratch/grub.cfg"
            sudo chroot "$_mnt" /usr/bin/grub-mkstandalone \
                --directory="/usr/lib/grub/$_efi_target" \
                --format="$_efi_target" \
                --output="/scratch/$_boot_efi" \
                --locales="" --fonts="" \
                "boot/grub/grub.cfg=/scratch/grub.cfg"
            sudo cp "$_mnt/scratch/$_boot_efi" "$_basedir/image_tree/scratch/$_boot_efi"
            sudo rm -rf "$_mnt/scratch"
            ;;
    esac

    sudo mkdir -p "$_mnt/boot/efi/EFI/BOOT"
    sudo cp "$_basedir/image_tree/scratch/$_boot_efi" "$_mnt/boot/efi/EFI/BOOT/$_boot_efi"
    if [ "$_arch" = "amd64" ] && [ -f "$_basedir/image_tree/scratch/BOOTIA32.EFI" ]; then
        sudo cp "$_basedir/image_tree/scratch/BOOTIA32.EFI" "$_mnt/boot/efi/EFI/BOOT/BOOTIA32.EFI"
    fi

    # Remove the /EFI/debian/ tree that grub-efi-amd64's postinst dropped.
    # Keeping it around lets OVMF persist a Boot#### entry pointing at
    # that stale binary on first run; subsequent boots then ignore our
    # standalone at /EFI/BOOT/BOOTX64.EFI and fail with "vmlinuz not found".
    sudo rm -rf "$_mnt/boot/efi/EFI/debian" "$_mnt/boot/efi/EFI/Debian"

    # First-boot resize: grow root partition to fill the target disk and
    # resize the ext4 FS. Enabled during install; the unit disables itself
    # after running so upgrades don't repeat the resize.
    sudo tee "$_mnt/usr/local/sbin/vos-resize-root" >/dev/null <<'RSZEOF'
#!/bin/sh
# First-boot only: grow the root partition to fill the target disk and
# resize the ext4 FS. Uses sfdisk (util-linux) and resize2fs (e2fsprogs),
# both guaranteed on any Debian install.
set -e
_root=$(findmnt -no SOURCE /)
_disk=$(lsblk -no PKNAME "$_root")
[ -n "$_disk" ] || exit 0
_partnum=$(echo "$_root" | sed 's|.*[^0-9]||')
echo ", +" | sfdisk -N "$_partnum" "/dev/$_disk" || true
partprobe "/dev/$_disk" 2>/dev/null || true
resize2fs "$_root" || true
systemctl disable vos-resize-root.service || true
RSZEOF
    sudo chmod +x "$_mnt/usr/local/sbin/vos-resize-root"
    sudo tee "$_mnt/etc/systemd/system/vos-resize-root.service" >/dev/null <<'UNITEOF'
[Unit]
Description=Grow root filesystem to fill disk (first boot)
DefaultDependencies=no
After=systemd-remount-fs.service
Before=local-fs-pre.target
Wants=local-fs-pre.target
ConditionPathExists=/usr/local/sbin/vos-resize-root

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/vos-resize-root
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNITEOF
    sudo chroot "$_mnt" systemctl enable vos-resize-root.service 2>/dev/null || true

    qemu_eject "$_mnt" "$_arch"

    _loop_image_cleanup
    trap - EXIT INT TERM

    log_step "Copying OVMF vars..."
    if [ -f /usr/share/OVMF/OVMF_VARS_4M.fd ]; then
        cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$_ovmf_vars"
        log_info "Copied OVMF_VARS.fd locally"
    else
        log_warn "OVMF_VARS_4M.fd not found. Install package 'ovmf'."
    fi

    log_info "RAW image created: $_raw"
}

create_iso() {
    _basedir="$1"
    _arch="$2"
    mkdir -p "$_basedir/output"
    _efi_target="$(arch_to_efi_target "$_arch")"
    _imagekernelversion=$(cat "$_basedir/imagekernelversion.conf" 2>/dev/null || die "imagekernelversion.conf not found. Run setupenv first.")

    BUILD_TYPE="Release"
    if [ -f "$_basedir/buildconfig.conf" ]; then
        . "$_basedir/buildconfig.conf"
        BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
    fi

    _count=$(ls -1 "$_basedir"/*.deb 2>/dev/null | wc -l)
    if [ "$_count" -eq 0 ]; then
        die "No deb files generated! Run cpack inside $_basedir/"
    fi

    _chroot_dir="$_basedir/image_tree/chroot"
    trap '_iso_cleanup' EXIT INT TERM
    sudo mkdir -p "$_chroot_dir/tmp" "$_chroot_dir/proc"
    sudo mount -o bind "$_basedir/" "$_chroot_dir/tmp/"
    sudo mount -t proc proc "$_chroot_dir/proc/"

    qemu_inject "$_chroot_dir" "$_arch"

    chroot_mount_deb_cache "$_chroot_dir" "$(chroot_cache_dir "$_basedir")"

    if [ -f "$_basedir/image_tree/image/live/filesystem.squashfs" ]; then
        log_info "Removing previous squashfs image..."
        rm -f "$_basedir/image_tree/image/live/filesystem.squashfs"
    fi

    _iso_pkgs="$(get_iso_image_packages "$_arch")"
    log_step "Installing debs into chroot..."
    sudo chroot "$_chroot_dir" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "set -e
apt remove -y vos nexus-dkms || true
apt-get install -y dkms build-essential linux-headers-$_imagekernelversion $_iso_pkgs
apt install -y -f --reinstall /tmp/*.deb
depmod -v $_imagekernelversion" || die "iso chroot bash-c failed (dpkg/kernel stage)"

    _common_chroot_setup "$_chroot_dir" "vitruvian" "" "" \
        || die "_common_chroot_setup failed"

    if [ "$BUILD_TYPE" = "Debug" ]; then
        log_step "Configuring SSH server for debug access..."
        sudo chroot "$_chroot_dir" /bin/bash -eux <<'SSHEOF'
export DEBIAN_FRONTEND=noninteractive
mkdir -p /etc/ssh/sshd_config.d
cat > /etc/ssh/sshd_config.d/debug.conf <<'EOF'
PermitRootLogin yes
PasswordAuthentication yes
PermitEmptyPasswords no
EOF
chmod 0644 /etc/ssh/sshd_config.d/debug.conf
chown root:root /etc/ssh/sshd_config.d/debug.conf
mkdir -p /root/.ssh
chmod 0700 /root/.ssh
chown root:root /root/.ssh
if command -v systemctl >/dev/null 2>&1; then
    systemctl enable ssh.service 2>/dev/null || true
fi
SSHEOF
        log_info "SSH server configured."
    fi

    qemu_eject "$_chroot_dir" "$_arch"

    _iso_cleanup
    trap - EXIT INT TERM

    log_step "Creating ISO structure..."
    sudo mkdir -p "$_basedir/image_tree/scratch"
    sudo mkdir -p "$_basedir/image_tree/image/live"
    sudo chown "$(id -u):$(id -g)" "$_basedir/image_tree/scratch" "$_basedir/image_tree/image" "$_basedir/image_tree/image/live"

    # Ensure empty mountpoint dirs exist in chroot (these were excluded from squashfs anyway)
    for _d in dev/hugepages dev/mqueue run/lock \
              sys/kernel/debug sys/kernel/tracing \
              sys/fs/fuse/connections sys/kernel/config; do
        sudo mkdir -p "$_chroot_dir/$_d"
    done

    log_step "Compressing chroot..."
    sudo mksquashfs \
        "$_chroot_dir" \
        "$_basedir/image_tree/image/live/filesystem.squashfs" \
        -b 1048576 -comp xz -Xdict-size 100% -xattrs -e boot

    log_step "Copying kernel and initramfs..."
    cp "$_chroot_dir/boot/vmlinuz-$_imagekernelversion" "$_basedir/image_tree/image/vmlinuz"
    cp "$_chroot_dir/boot/initrd.img-$_imagekernelversion" "$_basedir/image_tree/image/initrd"

    log_step "Creating GRUB menu..."
    cat <<'EOF' >"$_basedir/image_tree/scratch/grub.cfg"
insmod all_video
search --set=root --file /VITRUVIAN_CUSTOM
set default="0"
set timeout=1
set hidden_timeout=0
menuentry "Vitruvian Live" {
    linux /vmlinuz boot=live noeject quiet splash loglevel=3 systemd.show_status=false rd.udev.log_priority=3 console=tty0 console=ttyS0,115200 earlyprintk=ttyS0,115200 ignore_loglevel oops=panic panic_on_warn=1 panic=0
    initrd /initrd
}
EOF

    touch "$_basedir/image_tree/image/VITRUVIAN_CUSTOM"

    case "$_arch" in
        amd64)   _efi_name="bootx64.efi" ;;
        arm64)   _efi_name="bootaa64.efi" ;;
        riscv64) _efi_name="bootriscv64.efi" ;;
    esac

    log_step "Building EFI bootloader ($_efi_target)..."
    if [ "$_arch" = "amd64" ]; then
        grub-mkstandalone \
            --format="$_efi_target" \
            --output="$_basedir/image_tree/scratch/$_efi_name" \
            --locales="" \
            --fonts="" \
            "boot/grub/grub.cfg=$_basedir/image_tree/scratch/grub.cfg"
        if [ -d /usr/lib/grub/i386-efi ]; then
            log_step "Building 32-bit EFI bootloader (i386-efi)..."
            grub-mkstandalone \
                --format=i386-efi \
                --output="$_basedir/image_tree/scratch/bootia32.efi" \
                --locales="" \
                --fonts="" \
                "boot/grub/grub.cfg=$_basedir/image_tree/scratch/grub.cfg"
        fi
    else
        sudo mkdir -p "$_basedir/image_tree/chroot/scratch"
        sudo cp "$_basedir/image_tree/scratch/grub.cfg" "$_basedir/image_tree/chroot/scratch/"
        sudo chroot "$_basedir/image_tree/chroot" /usr/bin/grub-mkstandalone \
            --directory=/usr/lib/grub/arm64-efi \
            --format="$_efi_target" \
            --output="/scratch/$_efi_name" \
            --locales="" \
            --fonts="" \
            "boot/grub/grub.cfg=/scratch/grub.cfg"
        sudo cp "$_basedir/image_tree/chroot/scratch/$_efi_name" "$_basedir/image_tree/scratch/"
    fi

    log_step "Creating EFI boot image..."
    _saved_pwd="$PWD"
    cd "$_basedir/image_tree/scratch"
    dd if=/dev/zero of=efiboot.img bs=1M count=10
    sudo mkfs.vfat efiboot.img
    mmd -i efiboot.img efi efi/boot
    mcopy -i efiboot.img "./$_efi_name" ::efi/boot/
    if [ "$_arch" = "amd64" ] && [ -f "./bootia32.efi" ]; then
        mcopy -i efiboot.img "./bootia32.efi" ::efi/boot/
    fi

    if [ "$_arch" = "amd64" ]; then
        log_step "Building legacy BIOS bootloader..."
        grub-mkstandalone \
            --format=i386-pc \
            --output="$_basedir/image_tree/scratch/core.img" \
            --install-modules="linux normal iso9660 biosdisk memdisk search tar ls" \
            --modules="linux normal iso9660 biosdisk search" \
            --locales="" \
            --fonts="" \
            "boot/grub/grub.cfg=$_basedir/image_tree/scratch/grub.cfg"

        cat \
            /usr/lib/grub/i386-pc/cdboot.img \
            "$_basedir/image_tree/scratch/core.img" \
            > "$_basedir/image_tree/scratch/bios.img"
    fi

    cd "$_saved_pwd"

    log_step "Generating ISO..."
    # ESP is published as a real GPT partition (visible to strict UEFI per
    # issue #186) and El Torito's EFI alt-boot references that same partition
    # via --interval:appended_partition_2 — so there is exactly one ESP on
    # the image, not a duplicate embedded inside the ISO9660 tree.
    if [ "$_arch" = "amd64" ]; then
        xorriso \
            -as mkisofs \
            -iso-level 3 \
            -full-iso9660-filenames \
            -volid "VITRUVIAN_CUSTOM" \
            -eltorito-boot \
                boot/grub/bios.img \
                -no-emul-boot \
                -boot-load-size 4 \
                -boot-info-table \
                --eltorito-catalog boot/grub/boot.cat \
            --grub2-boot-info \
            --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
            -append_partition 2 0xef "$_basedir/image_tree/scratch/efiboot.img" \
            -appended_part_as_gpt \
            -eltorito-alt-boot \
            -e '--interval:appended_partition_2:all::' \
            -no-emul-boot \
            -isohybrid-gpt-basdat \
            -output "$_basedir/output/vitruvian-custom.iso" \
            -graft-points \
                "$_basedir/image_tree/image" \
                /boot/grub/bios.img="$_basedir/image_tree/scratch/bios.img"
    else
        xorriso \
            -as mkisofs \
            -iso-level 3 \
            -full-iso9660-filenames \
            -volid "VITRUVIAN_CUSTOM" \
            -append_partition 2 0xef "$_basedir/image_tree/scratch/efiboot.img" \
            -appended_part_as_gpt \
            -eltorito-alt-boot \
            -e '--interval:appended_partition_2:all::' \
            -no-emul-boot \
            -isohybrid-gpt-basdat \
            -output "$_basedir/output/vitruvian-custom.iso" \
            -graft-points \
                "$_basedir/image_tree/image"
    fi

    log_info "ISO created: $_basedir/output/vitruvian-custom.iso"
    log_info "Build type: $BUILD_TYPE"
    if [ "$BUILD_TYPE" = "Debug" ]; then
        log_info "Debug build - SSH: root@<guest-ip> (password: live)"
    fi
}

_common_chroot_setup() {
    _mnt="$1"
    _hostname="$2"
    sudo chroot "$_mnt" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "set -e
echo '$_hostname' > /etc/hostname
# Root locked; Installer's Advanced mode is the only way to set a root
# password on a target.
passwd -l root 2>/dev/null || true

# Live/raw markers — Installer strips these on --commit-setup.
mkdir -p /etc/vos
: > /etc/vos/live
: > /etc/vos/debug
if ! getent passwd vos-live >/dev/null; then
    useradd --system --create-home --home-dir /home/vos-live \\
        --shell /bin/bash --comment 'Vitruvian live/try persona' \\
        vos-live
    passwd -l vos-live
    for g in sudo video render input plugdev nexus; do
        getent group \$g >/dev/null && adduser vos-live \$g || true
    done
    # shadow-utils useradd copy_tree does not preserve user.* xattrs on
    # all Debian versions, so BEOS:TYPE and friends on skel files get
    # dropped — Tracker's \"New\" menu then can't identify templates and
    # falls back to only \"New Folder\". Re-sync from /etc/skel with
    # xattr-preserving cp to restore them.
    cp -a --preserve=all /etc/skel/. /home/vos-live/
    chown -R vos-live:vos-live /home/vos-live/
fi
# vos_login needs /dev/nexus for the pre-auth chain.
getent group nexus >/dev/null && \\
    getent passwd vos_login >/dev/null && \\
    adduser vos_login nexus >/dev/null 2>&1 || true

# Janus owns tty1's DRM VT for the graphical pre-auth chain; a getty on
# tty1 (autologin or not) would give vos-live / vos_login a shell before
# authentication whenever the graphical plane is not covering it.
# getty@tty2 stays enabled in postinst for recovery.
systemctl mask getty@tty1.service 2>/dev/null || true

# Mask units that are noisy on QEMU / Vitruvian and provide no value:
#  - systemd-remount-fs: we boot rw via cmdline, nothing to remount.
#  - systemd-ssh-generator: pokes AF_VSOCK CIDs that don't exist under
#    qemu user-mode networking; emits an error every boot.
#  - dev-hugepages/dev-mqueue/sys-fs-fuse-connections/sys-kernel-{config,debug,tracing}:
#    kernel pseudo-FS that this kernel build does not expose; the static
#    mount units fail every boot for no reason. Masking is the upstream
#    recommendation when the FS isn't available.

for _u in \\
    systemd-remount-fs.service \\
    systemd-ssh-generator.service \\
    dev-hugepages.mount \\
    dev-mqueue.mount \\
    sys-fs-fuse-connections.mount \\
    sys-kernel-config.mount \\
    sys-kernel-debug.mount \\
    sys-kernel-tracing.mount \\
    ctrl-alt-del.target; do
    systemctl mask \"\$_u\" 2>/dev/null || true
done" || die "_common_chroot_setup chroot bash-c failed"
}

create_raspberry() {
    _basedir="$1"
    _board="${2:-raspberry}"
    _board_arch="$(board_config "$_board" arch)"
    _deb_arch="$(arch_to_deb "$_board_arch")"
    _raw="$_basedir/output/vitruvian-$_board.raw"
    _mnt="/mnt/vitruvian"
    _hostname="vitruvian"
    _user=""
    _pass=""
    _boot_size=$(board_config "$_board" boot_size_mb)
    _dtb_files=$(board_config "$_board" dtb_files)
    _board_pkgs="$(get_board_packages "$_board")"

    mkdir -p "$_basedir/output"

    log_step "Creating $(board_config "$_board" label) RAW image..."
    qemu-img create "$_raw" 4G

    _loop=$(sudo losetup --show -f -P "$_raw")
    log_info "Loop device: $_loop"
    trap '_loop_image_cleanup' EXIT INT TERM

    sudo parted --script "$_loop" mklabel msdos
    sudo parted --script "$_loop" mkpart primary fat32 4MiB "$_boot_size"MiB
    sudo parted --script "$_loop" set 1 boot on
    sudo parted --script "$_loop" mkpart primary ext4 "$_boot_size"MiB 100%
    sudo partprobe "$_loop"
    sudo udevadm settle

    _boot_part="${_loop}p1"
    _root_part="${_loop}p2"

    sudo mkfs.vfat -F32 "$_boot_part"
    sudo mkfs.ext4 -F "$_root_part"

    sudo mkdir -p "$_mnt"
    sudo mount "$_root_part" "$_mnt"
    sudo mkdir -p "$_mnt/boot/firmware"
    sudo mount "$_boot_part" "$_mnt/boot/firmware"

    log_step "Bootstrapping Debian trixie ($_deb_arch) for $(board_config "$_board" label)..."
    sudo debootstrap --arch="$_deb_arch" --foreign trixie "$_mnt" http://deb.debian.org/debian

    sudo mount --bind /dev "$_mnt/dev"
    sudo mount --bind /proc "$_mnt/proc"
    sudo mount --bind /sys "$_mnt/sys"
    sudo cp -L /etc/resolv.conf "$_mnt/etc/resolv.conf"

    qemu_inject "$_mnt" "$_board_arch"
    log_step "Running debootstrap second stage..."
    sudo chroot "$_mnt" /debootstrap/debootstrap --second-stage

    if ls "$_basedir"/*.deb >/dev/null 2>&1; then
        sudo mkdir -p "$_mnt/localdeb"
        sudo cp "$_basedir"/*.deb "$_mnt/localdeb/"
    fi

    log_step "Configuring system..."
    sudo chroot "$_mnt" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "apt update && apt install -y $_board_pkgs
if ls /localdeb/*.deb >/dev/null 2>&1; then
    dpkg -i /localdeb/*.deb || apt-get -f install -y
fi" || die "raspberry chroot bash-c failed"

    _common_chroot_setup "$_mnt" "$_hostname" "$_user" "$_pass" \
        || die "_common_chroot_setup failed"

    _kver=$(ls "$_mnt/lib/modules" | head -n1)
    log_info "Kernel version: $_kver"

    log_step "Copying device trees..."
    for _dtb in $_dtb_files; do
        _dtb_path="$_mnt/usr/lib/linux-image-$_kver/$_dtb"
        if [ -f "$_dtb_path" ]; then
            sudo cp "$_dtb_path" "$_mnt/boot/firmware/"
            log_info "  copied $_dtb"
        else
            log_warn "  dtb not found: $_dtb_path"
        fi
    done

    log_step "Writing RPi boot config..."
    if [ "$_board_arch" = "arm64" ]; then
        sudo sh -c "cat > '$_mnt/boot/firmware/config.txt'" <<'RPICFG'
[all]
arm_64bit=1
kernel=vmlinuz
initramfs initrd.img followkernel
disable_overscan=1
hdmi_drive=2
dtoverlay=vc4-kms-v3d

[pi4]
kernel=kernel7l.img
initramfs initrd.img followkernel

[pi5]
kernel=kernel_2712.img
initramfs initrd.img followkernel
RPICFG
    else
        sudo sh -c "cat > '$_mnt/boot/firmware/config.txt'" <<'RPICFG32'
[all]
kernel=zImage
initramfs initrd.img followkernel
disable_overscan=1
hdmi_drive=2
dtoverlay=vc4-kms-v3d

[pi1]
kernel=kernel.img
initramfs initrd.img followkernel

[pi2]
kernel=kernel7.img
initramfs initrd.img followkernel

[pi3]
kernel=kernel8.img
initramfs initrd.img followkernel

[pi0]
kernel=kernel.img
initramfs initrd.img followkernel
RPICFG32
    fi

    sudo sh -c "cat > '$_mnt/boot/firmware/cmdline.txt'" <<CMDLINE
root=/dev/mmcblk0p2 rootfstype=ext4 rw rootwait console=serial0,115200 console=tty1 quiet splash loglevel=3
CMDLINE

    log_step "Copying kernel and initrd to boot firmware..."
    sudo cp "$_mnt/boot/vmlinuz-$_kver" "$_mnt/boot/firmware/vmlinuz"
    sudo cp "$_mnt/boot/initrd.img-$_kver" "$_mnt/boot/firmware/initrd.img"

    sudo sh -c "cat > '$_mnt/etc/fstab'" <<FSTAB
/dev/mmcblk0p2  /            ext4    defaults,noatime  0 1
/dev/mmcblk0p1  /boot/firmware vfat   defaults          0 2
FSTAB

    qemu_eject "$_mnt" "$_board_arch"

    _loop_image_cleanup
    trap - EXIT INT TERM

    log_info "$(board_config "$_board" label) image created: $_raw"
    log_info "Write to SD card: dd if=$_raw of=/dev/sdX bs=4M status=progress"
}

create_uboot_board() {
    _basedir="$1"
    _board="$2"
    _board_arch="$(board_config "$_board" arch)"
    _deb_arch="$(arch_to_deb "$_board_arch")"
    _label=$(board_config "$_board" label)
    _part_fmt=$(board_config "$_board" partition_fmt)
    _boot_size=$(board_config "$_board" boot_size_mb)
    _root_fs=$(board_config "$_board" root_fs)
    _spl_off=$(board_config "$_board" spl_offset_sectors)
    _uboot_off=$(board_config "$_board" uboot_offset_sectors)
    _dtb_files=$(board_config "$_board" dtb_files)
    _board_pkgs="$(get_board_packages "$_board")"

    _raw="$_basedir/output/vitruvian-$_board.raw"
    _mnt="/mnt/vitruvian"
    _hostname="vitruvian"
    _user=""
    _pass=""

    mkdir -p "$_basedir/output"

    log_step "Creating $_label RAW image..."
    qemu-img create "$_raw" 4G

    _loop=$(sudo losetup --show -f -P "$_raw")
    log_info "Loop device: $_loop"
    trap '_loop_image_cleanup' EXIT INT TERM

    if [ "$_part_fmt" = "gpt" ]; then
        sudo parted --script "$_loop" mklabel gpt
    else
        sudo parted --script "$_loop" mklabel msdos
    fi
    sudo parted --script "$_loop" mkpart primary fat32 4MiB "$_boot_size"MiB
    if [ "$_part_fmt" = "gpt" ]; then
        sudo parted --script "$_loop" set 1 esp on
    else
        sudo parted --script "$_loop" set 1 boot on
    fi
    sudo parted --script "$_loop" mkpart primary "$_root_fs" "$_boot_size"MiB 100%
    sudo partprobe "$_loop"
    sudo udevadm settle

    _boot_part="${_loop}p1"
    _root_part="${_loop}p2"

    sudo mkfs.vfat -F32 "$_boot_part"
    case "$_root_fs" in
        xfs)  sudo mkfs.xfs -f "$_root_part" ;;
        ext4) sudo mkfs.ext4 -F "$_root_part" ;;
    esac

    sudo mkdir -p "$_mnt"
    sudo mount "$_root_part" "$_mnt"
    sudo mkdir -p "$_mnt/boot"
    sudo mount "$_boot_part" "$_mnt/boot"

    log_step "Bootstrapping Debian trixie ($_deb_arch) for $_label..."
    sudo debootstrap --arch="$_deb_arch" --foreign trixie "$_mnt" http://deb.debian.org/debian

    sudo mount --bind /dev "$_mnt/dev"
    sudo mount --bind /proc "$_mnt/proc"
    sudo mount --bind /sys "$_mnt/sys"
    sudo cp -L /etc/resolv.conf "$_mnt/etc/resolv.conf"

    qemu_inject "$_mnt" "$_board_arch"
    log_step "Running debootstrap second stage..."
    sudo chroot "$_mnt" /debootstrap/debootstrap --second-stage

    if ls "$_basedir"/*.deb >/dev/null 2>&1; then
        sudo mkdir -p "$_mnt/localdeb"
        sudo cp "$_basedir"/*.deb "$_mnt/localdeb/"
    fi

    log_step "Configuring $_label system..."
    sudo chroot "$_mnt" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -c "apt update && apt install -y $_board_pkgs u-boot-menu
if ls /localdeb/*.deb >/dev/null 2>&1; then
    dpkg -i /localdeb/*.deb || apt-get -f install -y
fi" || die "uboot chroot bash-c failed"

    _common_chroot_setup "$_mnt" "$_hostname" "$_user" "$_pass" \
        || die "_common_chroot_setup failed"

    _kver=$(ls "$_mnt/lib/modules" | head -n1)
    log_info "Kernel version: $_kver"

    log_step "Copying device trees..."
    sudo mkdir -p "$_mnt/boot/dtbs"
    for _dtb in $_dtb_files; do
        _dtb_path="$_mnt/usr/lib/linux-image-$_kver/$_dtb"
        if [ -f "$_dtb_path" ]; then
            _dtb_dir=$(dirname "$_dtb")
            sudo mkdir -p "$_mnt/boot/dtbs/$_dtb_dir"
            sudo cp "$_dtb_path" "$_mnt/boot/dtbs/$_dtb"
            log_info "  copied $_dtb"
        else
            log_warn "  dtb not found: $_dtb_path"
        fi
    done

    log_step "Writing U-Boot extlinux config..."
    sudo mkdir -p "$_mnt/boot/extlinux"
    sudo sh -c "cat > '$_mnt/boot/extlinux/extlinux.conf'" <<'EXTLINUX'
menu title Vitruvian Boot
timeout 3
default vitruvian

label vitruvian
    menu label Vitruvian
    linux /vmlinuz
    initrd /initrd.img
    fdtdir /dtbs/
    append root=/dev/mmcblk0p2 rootfstype=EXTROOTFS rw rootwait console=ttyS2,1500000 quiet splash
EXTLINUX
    sudo sed -i "s/EXTROOTFS/$_root_fs/" "$_mnt/boot/extlinux/extlinux.conf"

    sudo cp "$_mnt/boot/vmlinuz-$_kver" "$_mnt/boot/vmlinuz"
    sudo cp "$_mnt/boot/initrd.img-$_kver" "$_mnt/boot/initrd.img"

    case "$_root_fs" in
        xfs)  _root_mkfs="xfs" ;;
        ext4) _root_mkfs="ext4" ;;
    esac
    sudo sh -c "cat > '$_mnt/etc/fstab'" <<FSTAB
/dev/mmcblk0p2  /        $_root_mkfs  defaults,noatime  0 1
/dev/mmcblk0p1  /boot    vfat         defaults          0 2
FSTAB

    qemu_eject "$_mnt" "$_board_arch"

    sudo umount -l "$_mnt/dev"  2>/dev/null || true
    sudo umount -l "$_mnt/proc" 2>/dev/null || true
    sudo umount -l "$_mnt/sys"  2>/dev/null || true
    sudo umount -l "$_mnt/boot" 2>/dev/null || true
    sudo umount -l "$_mnt"      2>/dev/null || true

    log_step "Flashing U-Boot/SPL..."
    _uboot_dir="$_basedir/firmware/$_board"
    if [ -d "$_uboot_dir" ]; then
        if [ -f "$_uboot_dir/idbloader.img" ]; then
            sudo dd if="$_uboot_dir/idbloader.img" of="$_loop" bs=512 seek="$_spl_off" conv=notrunc
            log_info "  SPL written at sector $_spl_off"
        fi
        if [ -f "$_uboot_dir/u-boot.itb" ]; then
            sudo dd if="$_uboot_dir/u-boot.itb" of="$_loop" bs=512 seek="$_uboot_off" conv=notrunc
            log_info "  U-Boot written at sector $_uboot_off"
        elif [ -f "$_uboot_dir/u-boot.bin" ]; then
            sudo dd if="$_uboot_dir/u-boot.bin" of="$_loop" bs=512 seek="$_uboot_off" conv=notrunc
            log_info "  U-Boot written at sector $_uboot_off"
        elif [ -f "$_uboot_dir/u-boot.img" ]; then
            sudo dd if="$_uboot_dir/u-boot.img" of="$_loop" bs=512 seek="$_uboot_off" conv=notrunc
            log_info "  U-Boot written at sector $_uboot_off"
        fi
        if [ -f "$_uboot_dir/trust.bin" ]; then
            sudo dd if="$_uboot_dir/trust.bin" of="$_loop" bs=512 seek=$(( _uboot_off + 2048 )) conv=notrunc
            log_info "  trust.bin written"
        fi
    else
        log_warn "No firmware found at $_uboot_dir"
        log_warn "U-Boot not flashed. Place idbloader.img + u-boot.itb in $_uboot_dir/"
    fi

    _loop_image_cleanup
    trap - EXIT INT TERM

    log_info "$_label image created: $_raw"
    log_info "Write to SD card: dd if=$_raw of=/dev/sdX bs=4M status=progress"
}
