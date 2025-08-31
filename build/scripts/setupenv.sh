#!/bin/sh
set -e

basedir=`realpath ./`

bold=$(tput bold)
normal=$(tput sgr0)

echo ${bold}Install dependencies...
echo ${normal}

sudo apt install -y debootstrap linux-headers-amd64 squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin mtools

echo  ${bold}Prepare Debian bootstrap and install packages...
echo ${normal}

mkdir -p $basedir/LIVE_BOOT
sudo debootstrap --arch=amd64 --variant=minbase bookworm $basedir/LIVE_BOOT/chroot http://ftp.us.debian.org/debian/

sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "echo "vitruvian-live" > /etc/hostname &\
apt update && apt install -y --no-install-recommends apt-utils dialog linux-image-amd64 live-boot systemd-sysv network-manager net-tools wireless-tools curl openssh-client procps vim-tiny libbinutils &&\
echo "root:vitruvio"|chpasswd; exit"

ls ./LIVE_BOOT/chroot/lib/modules | head -n1 > imagekernelversion.conf
uname -r > hostkernelversion.conf

echo ${bold}Success!
echo ${normal}
