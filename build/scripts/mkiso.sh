#!/bin/sh
export TERM=xterm-256color
set -e

basedir=`realpath ./`
imagekernelversion=`cat ./imagekernelversion.conf`

bold=$(tput bold)
normal=$(tput sgr0)

# Check if any local deb file exist
count=`ls -1 $basedir/*.deb 2>/dev/null | wc -l`
if [ $count != 0 ]; then
  sudo mount -o bind $basedir/ $basedir/LIVE_BOOT/chroot/tmp/
  sudo mount -t proc /proc ./LIVE_BOOT/chroot/proc/

  # Bind libraries for chroot build
  for libdir in lib lib64 usr/lib; do
    if [ -d "$basedir/LIVE_BOOT/chroot/$libdir" ]; then
      sudo mount --bind "$basedir/LIVE_BOOT/chroot/$libdir" "$basedir/LIVE_BOOT/chroot/$libdir"
    fi
  done

else
  echo ${bold}No deb file generated! Please run cpack inside /$basedir/
  echo ${normal}
  exit
fi

# Cleanup automatico dei mount
cleanup() {
  if mountpoint -q "$basedir/LIVE_BOOT/chroot/proc/"; then
    sudo umount "$basedir/LIVE_BOOT/chroot/proc/"
  fi
  if mountpoint -q "$basedir/LIVE_BOOT/chroot/tmp/"; then
    sudo umount "$basedir/LIVE_BOOT/chroot/tmp/"
  fi
  for libdir in lib lib64 usr/lib; do
    if mountpoint -q "$basedir/LIVE_BOOT/chroot/$libdir"; then
      sudo umount "$basedir/LIVE_BOOT/chroot/$libdir"
    fi
  done
}
trap cleanup EXIT

# Install local debs (including Nexus IPC) and required packages
sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "echo 'vitruvian-live' > /etc/hostname &\
apt update && \
apt install -y -f --reinstall /tmp/*.deb && \
apt install -y dkms build-essential linux-headers-$imagekernelversion && \
depmod -v $imagekernelversion && exit"

# Build Ninja project inside chroot
sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "
cd /tmp/project && \
cmake -G Ninja -DCMAKE_PREFIX_PATH=/usr .. && \
ninja
"

# Resto dello script rimane identico
if [ -f $basedir/LIVE_BOOT/image/live/filesystem.squashfs ]; then
    rm -f $basedir/LIVE_BOOT/image/live/filesystem.squashfs
fi 

mkdir -p $basedir/LIVE_BOOT/scratch
mkdir -p $basedir/LIVE_BOOT/image/live

sudo mksquashfs $basedir/LIVE_BOOT/chroot $basedir/LIVE_BOOT/image/live/filesystem.squashfs -b 1048576 -comp xz -Xdict-size 100% -e boot

cp $basedir/LIVE_BOOT/chroot/boot/vmlinuz-* $basedir/LIVE_BOOT/image/vmlinuz
cp $basedir/LIVE_BOOT/chroot/boot/initrd.img-* $basedir/LIVE_BOOT/image/initrd

cat <<'EOF' >$basedir/LIVE_BOOT/scratch/grub.cfg
insmod all_video
search --set=root --file /VITRUVIAN_CUSTOM
set default="0"
set timeout=0
set hidden_timeout=0
menuentry "Vitruvian Live" {
    linux /vmlinuz boot=live quiet splash
    initrd /initrd
}
EOF

touch $basedir/LIVE_BOOT/image/VITRUVIAN_CUSTOM

grub-mkstandalone --format=x86_64-efi --output=$basedir/LIVE_BOOT/scratch/bootx64.efi --locales="" --fonts="" "boot/grub/grub.cfg=$basedir/LIVE_BOOT/scratch/grub.cfg"

cd $basedir/LIVE_BOOT/scratch
dd if=/dev/zero of=efiboot.img bs=1M count=10
sudo mkfs.vfat efiboot.img
mmd -i efiboot.img efi efi/boot
mcopy -i efiboot.img ./bootx64.efi ::efi/boot/

grub-mkstandalone --format=i386-pc --output=$basedir/LIVE_BOOT/scratch/core.img --install-modules="linux normal iso9660 biosdisk memdisk search tar ls" --modules="linux normal iso9660 biosdisk search" --locales="" --fonts="" "boot/grub/grub.cfg=$basedir/LIVE_BOOT/scratch/grub.cfg"

cat /usr/lib/grub/i386-pc/cdboot.img $basedir/LIVE_BOOT/scratch/core.img > $basedir/LIVE_BOOT/scratch/bios.img

xorriso -as mkisofs -iso-level 3 -full-iso9660-filenames -volid "VITRUVIAN_CUSTOM" \
  -eltorito-boot boot/grub/bios.img -no-emul-boot -boot-load-size 4 -boot-info-table --eltorito-catalog boot/grub/boot.cat \
  --grub2-boot-info --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
  -eltorito-alt-boot -e EFI/efiboot.img -no-emul-boot -append_partition 2 0xef $basedir/LIVE_BOOT/scratch/efiboot.img \
  -output "$basedir/LIVE_BOOT/vitruvian-custom.iso" \
  -graft-points "$basedir/LIVE_BOOT/image" /boot/grub/bios.img=$basedir/LIVE_BOOT/scratch/bios.img /EFI/efiboot.img=$basedir/LIVE_BOOT/scratch/efiboot.img

echo ${bold}Finished!
