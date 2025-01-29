#!/bin/sh

set -e

basedir=`realpath ./`

echo $basedir > ./basedir.conf

bold=$(tput bold)
normal=$(tput sgr0)

echo ${bold}Install Dependencies...
echo ${normal}

sudo apt install debootstrap squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin mtools

echo  ${bold}Prepare Debian Bootstrap...
echo ${normal}

mkdir -p $basedir/LIVE_BOOT
sudo debootstrap --arch=amd64 --variant=minbase bookworm $basedir/LIVE_BOOT/chroot http://ftp.us.debian.org/debian/

echo ${bold}Vitruvian Building inside the Chroot Environment...
echo ${normal}

sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "echo "vitruvian-live" > /etc/hostname &\
apt update && apt install -y --no-install-recommends apt-utils dialog linux-image-amd64 live-boot systemd-sysv network-manager net-tools wireless-tools curl openssh-client procps vim-tiny libbinutils &&\
passwd; exit"&\

echo ${bold}Success!
echo ${normal}
