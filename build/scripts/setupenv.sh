#!/bin/sh
set -e

basedir=`realpath ./`

bold=$(tput bold)
normal=$(tput sgr0)

echo  ${bold}Prepare Debian bootstrap and install packages...
echo ${normal}

if [ -d "$basedir/image_tree/chroot" ]; then
  ts=$(date +%Y%m%d-%H%M%S)
  backup="$basedir/image_tree/chroot.old-$ts"
  echo "Found existing chroot, moving to $backup"
  sudo umount -l $basedir/image_tree/chroot/proc 2>/dev/null || true
  sudo umount -l $basedir/image_tree/chroot/sys 2>/dev/null || true
  sudo umount -l $basedir/image_tree/chroot/dev/pts 2>/dev/null || true
  sudo umount -l $basedir/image_tree/chroot/dev 2>/dev/null || true
  for libdir in lib lib64 usr/lib; do
    if mountpoint -q "$basedir/image_tree/chroot/$libdir"; then
      sudo umount -l "$basedir/image_tree/chroot/$libdir"
    fi
  done
  sudo mv "$basedir/image_tree/chroot" "$backup"
fi

mkdir -p $basedir/image_tree
sudo debootstrap --arch=amd64 --variant=minbase trixie $basedir/image_tree/chroot http://deb.debian.org/debian/

sudo mount -t proc / $basedir/image_tree/chroot/proc
sudo mount --rbind /sys $basedir/image_tree/chroot/sys
sudo mount --make-rslave $basedir/image_tree/chroot/sys
sudo mount --rbind /dev $basedir/image_tree/chroot/dev
sudo mount --make-rslave $basedir/image_tree/chroot/dev

for libdir in lib lib64 usr/lib; do
  if [ -d "$basedir/image_tree/chroot/$libdir" ]; then
    sudo mount --bind "$basedir/image_tree/chroot/$libdir" "$basedir/image_tree/chroot/$libdir"
  fi
done

cleanup() {
  echo "Unmounting chroot mounts..."
  sudo umount -l $basedir/image_tree/chroot/proc || true
  sudo umount -l $basedir/image_tree/chroot/sys || true
  sudo umount -l $basedir/image_tree/chroot/dev || true
  for libdir in lib lib64 usr/lib; do
    if mountpoint -q "$basedir/image_tree/chroot/$libdir"; then
      sudo umount -l "$basedir/image_tree/chroot/$libdir"
    fi
  done
}
trap cleanup EXIT

BASE_PACKAGES="apt-utils curl dialog libbinutils linux-image-rt-amd64 live-boot locales net-tools network-manager openssh-client openssh-server pipewire-audio pipewire-bin procps systemd-sysv vim-tiny wireless-tools wireplumber xfsprogs"
# Development packages needed for isolated chroot builds
DEV_PACKAGES="linux-headers-rt-amd64 pkg-config libc6-dev libstdc++-14-dev libfreetype6-dev libicu-dev libdrm-dev libinput-dev libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev libblkid-dev libbacktrace-dev libfl-dev libncurses-dev libgl-dev libegl-dev libgbm-dev"

sudo chroot $basedir/image_tree/chroot /bin/bash -c "echo 'vitruvian' > /etc/hostname && \
apt update && apt install -y --no-install-recommends $BASE_PACKAGES $DEV_PACKAGES $DEBUG_PACKAGES && \
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen && locale-gen && \
exit"

ls ./image_tree/chroot/lib/modules | head -n1 > imagekernelversion.conf

echo ${normal}
