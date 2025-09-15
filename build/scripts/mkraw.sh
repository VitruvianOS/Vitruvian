#!/bin/bash
set -e

RAW="output/vitruvian.raw"
MNT="/mnt/vitruvian"
HOSTNAME="vitruvian"
USER="vitruvio"
PASS="vitruvio"
OVMF_VARS="./OVMF_VARS.fd"

# Shared folder
HOST_SHARED_DIR="$PWD/shared"
GUEST_MOUNT_POINT="/mnt/host_shared"
mkdir -p "$HOST_SHARED_DIR"

mkdir -p output

# 1. Create empty RAW image (4 GiB)
echo "[+] Creating RAW image"
qemu-img create "$RAW" 4G

# 2. Attach loop device with partitions
LOOP=$(sudo losetup -fP --show "$RAW")
echo "[+] Loop device: $LOOP"

# 3. Partition table: EFI (512M FAT32) + root (rest ext4)
sudo parted --script "$LOOP" mklabel gpt
sudo parted --script "$LOOP" mkpart ESP fat32 1MiB 513MiB
sudo parted --script "$LOOP" set 1 esp on
sudo parted --script "$LOOP" mkpart primary ext4 513MiB 100%
sudo partprobe "$LOOP"

EFI_PART="${LOOP}p1"
ROOT_PART="${LOOP}p2"

# 4. Format filesystems
sudo mkfs.vfat -F32 "$EFI_PART"
sudo mkfs.ext4 -F "$ROOT_PART"

# 5. Mount root
sudo mkdir -p "$MNT"
sudo mount "$ROOT_PART" "$MNT"

# 6. Create /boot/efi and mount ESP
sudo mkdir -p "$MNT/boot/efi"
sudo mount "$EFI_PART" "$MNT/boot/efi"

# 7. Bootstrap Debian
sudo debootstrap --arch=amd64 bookworm "$MNT" http://deb.debian.org/debian

# 8. Copy local deb packages into target if present
if ls "$PWD"/*.deb >/dev/null 2>&1; then
    sudo mkdir -p "$MNT/localdeb"
    sudo cp "$PWD"/*.deb "$MNT/localdeb/"
fi

# 9. Bind mounts for chroot
sudo mount --bind /dev "$MNT/dev"
sudo mount --bind /proc "$MNT/proc"
sudo mount --bind /sys "$MNT/sys"

# 10. Configure inside chroot
sudo chroot "$MNT" /bin/bash <<EOF
set -e
echo "$HOSTNAME" > /etc/hostname
echo "root:$PASS" | chpasswd
useradd -m -s /bin/bash $USER
echo "$USER:$PASS" | chpasswd
adduser $USER sudo

cat > /etc/apt/sources.list <<EOL
deb http://deb.debian.org/debian bookworm main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security bookworm-security main contrib non-free non-free-firmware
EOL

apt update
apt install -y systemd systemd-sysv sudo vim net-tools iproute2 openssh-server \
    linux-image-amd64 grub-efi-amd64 grub-efi-amd64-bin grub-efi-amd64-signed shim-signed efibootmgr

# Install local deb packages if available
if ls /localdeb/*.deb >/dev/null 2>&1; then
    dpkg -i /localdeb/*.deb || apt-get -f install -y
fi

# Install GRUB EFI
grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=debian --recheck
update-grub

# Silent boot
sed -i 's/GRUB_TIMEOUT=[0-9]\\+/GRUB_TIMEOUT=0/' /etc/default/grub
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="[^"]*/GRUB_CMDLINE_LINUX_DEFAULT="quiet splash loglevel=3 systemd.show_status=0 rd.udev.log_priority=3/' /etc/default/grub
update-grub

# Autologin on tty1
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf <<EOL
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noissue --autologin $USER %I \$TERM
TTYVTDisallocate=no
EOL

# fstab
cat > /etc/fstab <<EOL
/dev/vda2   /         ext4    errors=remount-ro  0 1
/dev/vda1   /boot/efi vfat    umask=0077         0 1
host_shared $GUEST_MOUNT_POINT 9p trans=virtio,version=9p2000.L,rw 0 0
EOL

# Prepare guest mount point for shared folder
mkdir -p $GUEST_MOUNT_POINT
EOF

# 11. Cleanup chroot mounts
sudo umount "$MNT/dev" || true
sudo umount "$MNT/proc" || true
sudo umount "$MNT/sys" || true

# 12. Prepare BOOTX64.EFI for first boot
sudo mkdir -p "$MNT/boot/efi/EFI/BOOT"
sudo cp "$MNT/boot/efi/EFI/debian/grubx64.efi" "$MNT/boot/efi/EFI/BOOT/BOOTX64.EFI"

# 13. Unmount ESP and root, detach loop
sudo umount "$MNT/boot/efi" || true
sudo umount "$MNT"
sudo losetup -d "$LOOP"

# 14. Copy OVMF_VARS.fd locally
if [ -f /usr/share/OVMF/OVMF_VARS_4M.fd ]; then
    cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$OVMF_VARS"
    echo "[+] Copied OVMF_VARS.fd locally for QEMU"
else
    echo "[!] Warning: /usr/share/OVMF/OVMF_VARS_4M.fd not found. Install package 'ovmf'."
    exit 1
fi

# 15. Final QEMU instructions
echo "[+] Done! You can now boot with:"
echo "qemu-system-x86_64 -m 2048 -smp 2 \\"
echo "  -drive file=$RAW,format=raw,if=virtio \\"
echo "  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \\"
echo "  -drive if=pflash,format=raw,file=$OVMF_VARS \\"
echo "  -virtfs local,path=$HOST_SHARED_DIR,mount_tag=host_shared,security_model=mapped-xattr,id=host_shared"
