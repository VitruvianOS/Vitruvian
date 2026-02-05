#!/bin/sh
set -e

basedir=`realpath ./`

bold=$(tput bold)
normal=$(tput sgr0)

echo  ${bold}Prepare Debian bootstrap and install packages...
echo ${normal}

if [ -d "$basedir/LIVE_BOOT/chroot" ]; then
  ts=$(date +%Y%m%d-%H%M%S)
  backup="$basedir/LIVE_BOOT/chroot.old-$ts"
  echo "Found existing chroot, moving to $backup"
  sudo umount -l $basedir/LIVE_BOOT/chroot/proc 2>/dev/null || true
  sudo umount -l $basedir/LIVE_BOOT/chroot/sys 2>/dev/null || true
  sudo umount -l $basedir/LIVE_BOOT/chroot/dev/pts 2>/dev/null || true
  sudo umount -l $basedir/LIVE_BOOT/chroot/dev 2>/dev/null || true
  for libdir in lib lib64 usr/lib; do
    if mountpoint -q "$basedir/LIVE_BOOT/chroot/$libdir"; then
      sudo umount -l "$basedir/LIVE_BOOT/chroot/$libdir"
    fi
  done
  sudo mv "$basedir/LIVE_BOOT/chroot" "$backup"
fi

mkdir -p $basedir/LIVE_BOOT
sudo debootstrap --arch=amd64 --variant=minbase trixie $basedir/LIVE_BOOT/chroot http://deb.debian.org/debian/

sudo mount -t proc / $basedir/LIVE_BOOT/chroot/proc
sudo mount --rbind /sys $basedir/LIVE_BOOT/chroot/sys
sudo mount --make-rslave $basedir/LIVE_BOOT/chroot/sys
sudo mount --rbind /dev $basedir/LIVE_BOOT/chroot/dev
sudo mount --make-rslave $basedir/LIVE_BOOT/chroot/dev

for libdir in lib lib64 usr/lib; do
  if [ -d "$basedir/LIVE_BOOT/chroot/$libdir" ]; then
    sudo mount --bind "$basedir/LIVE_BOOT/chroot/$libdir" "$basedir/LIVE_BOOT/chroot/$libdir"
  fi
done

cleanup() {
  echo "Unmounting chroot mounts..."
  sudo umount -l $basedir/LIVE_BOOT/chroot/proc || true
  sudo umount -l $basedir/LIVE_BOOT/chroot/sys || true
  sudo umount -l $basedir/LIVE_BOOT/chroot/dev || true
  for libdir in lib lib64 usr/lib; do
    if mountpoint -q "$basedir/LIVE_BOOT/chroot/$libdir"; then
      sudo umount -l "$basedir/LIVE_BOOT/chroot/$libdir"
    fi
  done
}
trap cleanup EXIT

BASE_PACKAGES="apt-utils dialog linux-image-amd64 live-boot systemd-sysv network-manager net-tools wireless-tools curl openssh-client procps vim-tiny libbinutils openssh-server"

sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "echo 'vitruvian-live' > /etc/hostname && \
apt update && apt install -y --no-install-recommends $BASE_PACKAGES $DEBUG_PACKAGES && \
exit"

ls ./LIVE_BOOT/chroot/lib/modules | head -n1 > imagekernelversion.conf

echo ${normal}
