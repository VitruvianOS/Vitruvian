#!/bin/sh

create_raw() {
    _basedir="$1"
    _arch="$2"
    _deb_arch="$(arch_to_deb "$_arch")"
    _efi_target="$(arch_to_efi_target "$_arch")"
    if [ -z "$_efi_target" ]; then
        die "EFI raw images not supported on $_arch. Use a board-specific image type instead."
    fi
    _raw="$_basedir/output/vitruvian.raw"
    _mnt="/mnt/vitruvian"
    _hostname="vitruvian"
    _user="vitruvio"
    _pass="vitruvio"
    _ovmf_vars="$_basedir/OVMF_VARS.fd"
    _host_shared="$_basedir/shared"
    _guest_mnt="/mnt/host_shared"

    mkdir -p "$_basedir/output"
    mkdir -p "$_host_shared"

    log_step "Creating RAW image..."
    qemu-img create "$_raw" 4G

    _loop=$(sudo losetup -fP --show "$_raw")
    log_info "Loop device: $_loop"

    sudo parted --script "$_loop" mklabel gpt
    sudo parted --script "$_loop" mkpart ESP fat32 1MiB 513MiB
    sudo parted --script "$_loop" set 1 esp on
    sudo parted --script "$_loop" mkpart primary xfs 513MiB 100%
    sudo partprobe "$_loop"

    _efi_part="${_loop}p1"
    _root_part="${_loop}p2"

    sudo mkfs.vfat -F32 "$_efi_part"
    sudo mkfs.xfs -f "$_root_part"

    sudo mkdir -p "$_mnt"
    sudo mount "$_root_part" "$_mnt"
    sudo mkdir -p "$_mnt/boot/efi"
    sudo mount "$_efi_part" "$_mnt/boot/efi"

    if is_cross_build "$_arch"; then
        log_step "Bootstrapping Debian trixie ($_deb_arch) into RAW [foreign]..."
        sudo debootstrap --arch="$_deb_arch" --foreign trixie "$_mnt" http://deb.debian.org/debian
        sudo mount --bind /dev "$_mnt/dev"
        sudo mount --bind /proc "$_mnt/proc"
        sudo mount --bind /sys "$_mnt/sys"
        qemu_inject "$_mnt" "$_arch"
        log_step "Running debootstrap second stage..."
        sudo chroot "$_mnt" /debootstrap/debootstrap --second-stage
    else
        log_step "Bootstrapping Debian trixie ($_deb_arch) into RAW..."
        sudo debootstrap --arch="$_deb_arch" trixie "$_mnt" http://deb.debian.org/debian
        sudo mount --bind /dev "$_mnt/dev"
        sudo mount --bind /proc "$_mnt/proc"
        sudo mount --bind /sys "$_mnt/sys"
    fi

    if ls "$_basedir"/*.deb >/dev/null 2>&1; then
        sudo mkdir -p "$_mnt/localdeb"
        sudo cp "$_basedir"/*.deb "$_mnt/localdeb/"
    fi

    _raw_pkgs="$(get_raw_image_packages "$_arch")"

    log_step "Configuring system inside RAW chroot..."
    sudo chroot "$_mnt" /bin/bash -c "set -e
echo '$_hostname' > /etc/hostname
echo 'root:$_pass' | chpasswd
useradd -m -s /bin/bash $_user
echo '$_user:$_pass' | chpasswd
adduser $_user sudo

cat > /etc/apt/sources.list <<'SRCEOF'
deb http://deb.debian.org/debian trixie main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware
SRCEOF

apt update
apt install -y $_raw_pkgs

if ls /localdeb/*.deb >/dev/null 2>&1; then
    dpkg -i /localdeb/*.deb || apt-get -f install -y
fi

grub-install --target=$_efi_target --efi-directory=/boot/efi --bootloader-id=debian --recheck
update-grub

sed -i 's/GRUB_TIMEOUT=[0-9]\\\+/GRUB_TIMEOUT=0/' /etc/default/grub
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT=\"[^\"]*/GRUB_CMDLINE_LINUX_DEFAULT=\"quiet splash loglevel=3 systemd.show_status=0 rd.udev.log_priority=3/' /etc/default/grub
update-grub

mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf <<'LOGEOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noissue --autologin $_user %I \$TERM
TTYVTDisallocate=no
LOGEOF

cat > /etc/fstab <<'FSTABEOF'
/dev/vda2   /         xfs     defaults           0 1
/dev/vda1   /boot/efi vfat    umask=0077         0 1
host_shared $_guest_mnt 9p trans=virtio,version=9p2000.L,rw 0 0
FSTABEOF

mkdir -p $_guest_mnt"

    qemu_eject "$_mnt" "$_arch"

    sudo umount "$_mnt/dev" || true
    sudo umount "$_mnt/proc" || true
    sudo umount "$_mnt/sys" || true

    case "$_arch" in
        amd64)
            _boot_efi="BOOTX64.EFI"
            _src_efi="grubx64.efi"
            ;;
        arm64)
            _boot_efi="BOOTAA64.EFI"
            _src_efi="grubaa64.efi"
            ;;
        riscv64)
            _boot_efi="BOOTRISCV64.EFI"
            _src_efi="grubriscv64.efi"
            ;;
    esac

    sudo mkdir -p "$_mnt/boot/efi/EFI/BOOT"
    sudo cp "$_mnt/boot/efi/EFI/debian/$_src_efi" "$_mnt/boot/efi/EFI/BOOT/$_boot_efi"

    sudo umount "$_mnt/boot/efi" || true
    sudo umount "$_mnt" || true
    sudo losetup -d "$_loop"

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
    sudo mount -o bind "$_basedir/" "$_chroot_dir/tmp/"
    sudo mount -t proc /proc "$_chroot_dir/proc/"

    qemu_inject "$_chroot_dir" "$_arch"

    if [ -f "$_basedir/image_tree/image/live/filesystem.squashfs" ]; then
        log_info "Removing previous squashfs image..."
        rm -f "$_basedir/image_tree/image/live/filesystem.squashfs"
    fi

    log_step "Installing debs into chroot..."
    sudo chroot "$_chroot_dir" /bin/bash -c "echo 'vitruvian' > /etc/hostname && \
apt remove -y vos nexus-dkms || true && \
apt-get install -y dkms build-essential linux-headers-$_imagekernelversion && \
apt install -y -f --reinstall /tmp/*.deb && \
depmod -v $_imagekernelversion && echo 'root:live' | chpasswd; exit"

    if [ "$BUILD_TYPE" = "Debug" ]; then
        log_step "Configuring SSH server for debug access..."
        sudo chroot "$_chroot_dir" /bin/bash -eux <<'SSHEOF'
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

    if mountpoint -q "$_chroot_dir/proc/" 2>/dev/null; then
        sudo umount "$_chroot_dir/proc/" || true
    fi
    if mountpoint -q "$_chroot_dir/tmp/" 2>/dev/null; then
        sudo umount "$_chroot_dir/tmp/" || true
    fi

    log_step "Creating ISO structure..."
    mkdir -p "$_basedir/image_tree/scratch"
    mkdir -p "$_basedir/image_tree/image/live"

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
    linux /vmlinuz boot=live quiet splash
    initrd /initrd
}
EOF

    touch "$_basedir/image_tree/image/VITRUVIAN_CUSTOM"

    log_step "Building EFI bootloader ($_efi_target)..."
    grub-mkstandalone \
        --format="$_efi_target" \
        --output="$_basedir/image_tree/scratch/bootx64.efi" \
        --locales="" \
        --fonts="" \
        "boot/grub/grub.cfg=$_basedir/image_tree/scratch/grub.cfg"

    log_step "Creating EFI boot image..."
    _saved_pwd="$PWD"
    cd "$_basedir/image_tree/scratch"
    dd if=/dev/zero of=efiboot.img bs=1M count=10
    sudo mkfs.vfat efiboot.img
    mmd -i efiboot.img efi efi/boot
    mcopy -i efiboot.img ./bootx64.efi ::efi/boot/

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
            -eltorito-alt-boot \
            -e EFI/efiboot.img \
            -no-emul-boot \
            -isohybrid-gpt-basdat \
            -append_partition 2 0xef "$_basedir/image_tree/scratch/efiboot.img" \
            -output "$_basedir/image_tree/vitruvian-custom.iso" \
            -graft-points \
                "$_basedir/image_tree/image" \
                /boot/grub/bios.img="$_basedir/image_tree/scratch/bios.img" \
                /EFI/efiboot.img="$_basedir/image_tree/scratch/efiboot.img"
    else
        xorriso \
            -as mkisofs \
            -iso-level 3 \
            -full-iso9660-filenames \
            -volid "VITRUVIAN_CUSTOM" \
            -eltorito-alt-boot \
            -e EFI/efiboot.img \
            -no-emul-boot \
            -isohybrid-gpt-basdat \
            -append_partition 2 0xef "$_basedir/image_tree/scratch/efiboot.img" \
            -output "$_basedir/image_tree/vitruvian-custom.iso" \
            -graft-points \
                "$_basedir/image_tree/image" \
                /EFI/efiboot.img="$_basedir/image_tree/scratch/efiboot.img"
    fi

    log_info "ISO created: $_basedir/image_tree/vitruvian-custom.iso"
    log_info "Build type: $BUILD_TYPE"
    if [ "$BUILD_TYPE" = "Debug" ]; then
        log_info "Debug build - SSH: root@<guest-ip> (password: live)"
    fi
}

_common_chroot_setup() {
    _mnt="$1"
    _hostname="$2"
    _user="$3"
    _pass="$4"
    sudo chroot "$_mnt" /bin/bash -c "set -e
echo '$_hostname' > /etc/hostname
echo 'root:$_pass' | chpasswd
id -u $_user >/dev/null 2>&1 || useradd -m -s /bin/bash $_user
echo '$_user:$_pass' | chpasswd
adduser $_user sudo 2>/dev/null || true

cat > /etc/apt/sources.list <<'SRCEOF'
deb http://deb.debian.org/debian trixie main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware
SRCEOF

mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf <<'LOGEOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noissue --autologin $_user %I \$TERM
TTYVTDisallocate=no
LOGEOF"
}

create_raspberry() {
    _basedir="$1"
    _board="${2:-raspberry}"
    _board_arch="$(board_config "$_board" arch)"
    _deb_arch="$(arch_to_deb "$_board_arch")"
    _raw="$_basedir/output/vitruvian-$_board.raw"
    _mnt="/mnt/vitruvian"
    _hostname="vitruvian"
    _user="vitruvio"
    _pass="vitruvio"
    _boot_size=$(board_config "$_board" boot_size_mb)
    _dtb_files=$(board_config "$_board" dtb_files)
    _board_pkgs="$(get_board_packages "$_board")"

    mkdir -p "$_basedir/output"

    log_step "Creating $(board_config "$_board" label) RAW image..."
    qemu-img create "$_raw" 4G

    _loop=$(sudo losetup -fP --show "$_raw")
    log_info "Loop device: $_loop"

    sudo parted --script "$_loop" mklabel msdos
    sudo parted --script "$_loop" mkpart primary fat32 4MiB "$_boot_size"MiB
    sudo parted --script "$_loop" set 1 boot on
    sudo parted --script "$_loop" mkpart primary ext4 "$_boot_size"MiB 100%
    sudo partprobe "$_loop"

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

    qemu_inject "$_mnt" "$_board_arch"
    log_step "Running debootstrap second stage..."
    sudo chroot "$_mnt" /debootstrap/debootstrap --second-stage

    if ls "$_basedir"/*.deb >/dev/null 2>&1; then
        sudo mkdir -p "$_mnt/localdeb"
        sudo cp "$_basedir"/*.deb "$_mnt/localdeb/"
    fi

    log_step "Configuring system..."
    _common_chroot_setup "$_mnt" "$_hostname" "$_user" "$_pass"

    sudo chroot "$_mnt" /bin/bash -c "apt update && apt install -y $_board_pkgs
if ls /localdeb/*.deb >/dev/null 2>&1; then
    dpkg -i /localdeb/*.deb || apt-get -f install -y
fi"

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

    sudo umount "$_mnt/dev" || true
    sudo umount "$_mnt/proc" || true
    sudo umount "$_mnt/sys" || true
    sudo umount "$_mnt/boot/firmware" || true
    sudo umount "$_mnt" || true
    sudo losetup -d "$_loop"

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
    _user="vitruvio"
    _pass="vitruvio"

    mkdir -p "$_basedir/output"

    log_step "Creating $_label RAW image..."
    qemu-img create "$_raw" 4G

    _loop=$(sudo losetup -fP --show "$_raw")
    log_info "Loop device: $_loop"

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

    qemu_inject "$_mnt" "$_board_arch"
    log_step "Running debootstrap second stage..."
    sudo chroot "$_mnt" /debootstrap/debootstrap --second-stage

    if ls "$_basedir"/*.deb >/dev/null 2>&1; then
        sudo mkdir -p "$_mnt/localdeb"
        sudo cp "$_basedir"/*.deb "$_mnt/localdeb/"
    fi

    log_step "Configuring $_label system..."
    _common_chroot_setup "$_mnt" "$_hostname" "$_user" "$_pass"

    sudo chroot "$_mnt" /bin/bash -c "apt update && apt install -y $_board_pkgs u-boot-menu
if ls /localdeb/*.deb >/dev/null 2>&1; then
    dpkg -i /localdeb/*.deb || apt-get -f install -y
fi"

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

    sudo umount "$_mnt/dev" || true
    sudo umount "$_mnt/proc" || true
    sudo umount "$_mnt/sys" || true
    sudo umount "$_mnt/boot" || true
    sudo umount "$_mnt" || true

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

    sudo losetup -d "$_loop"

    log_info "$_label image created: $_raw"
    log_info "Write to SD card: dd if=$_raw of=/dev/sdX bs=4M status=progress"
}
