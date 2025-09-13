#!/bin/sh
set -e

basedir=`realpath ./`

bold=$(tput bold)
normal=$(tput sgr0)

echo  ${bold}Prepare Debian bootstrap and install packages...
echo ${normal}

mkdir -p $basedir/LIVE_BOOT
sudo debootstrap --arch=amd64 --variant=minbase trixie $basedir/LIVE_BOOT/chroot http://ftp.us.debian.org/debian/

# Bind librerie fondamentali per compilare dentro chroot
for libdir in lib lib64 usr/lib; do
  if [ -d "$basedir/LIVE_BOOT/chroot/$libdir" ]; then
    sudo mount --bind "$basedir/LIVE_BOOT/chroot/$libdir" "$basedir/LIVE_BOOT/chroot/$libdir"
  fi
done

sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "echo "vitruvian-live" > /etc/hostname &\
apt update && apt install -y --no-install-recommends apt-utils dialog linux-image-amd64 live-boot systemd-sysv network-manager net-tools wireless-tools curl openssh-client procps vim-tiny libbinutils &&\
echo "root:vitruvio"|chpasswd; exit"

ls ./LIVE_BOOT/chroot/lib/modules | head -n1 > imagekernelversion.conf
uname -r > hostkernelversion.conf

echo ${bold}Success!
echo ${normal}
