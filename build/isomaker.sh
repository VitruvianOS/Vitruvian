
#!/bin/sh

bold=$(tput bold)
normal=$(tput sgr0)

echo ${bold}Install Dependencies...
echo ${normal}

sudo apt install debootstrap squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin mtools

echo  ${bold}Prepare Debian Bootstrap...
echo ${normal}

mkdir -p $HOME/LIVE_BOOT
sudo debootstrap --arch=amd64 --variant=minbase buster $HOME/LIVE_BOOT/chroot http://ftp.us.debian.org/debian/

echo ${bold}Vitruvian Building inside the Chroot Environment...
echo ${normal}

sudo chroot $HOME/LIVE_BOOT/chroot /bin/bash -c "echo "vitruvian-live" > /etc/hostname &\
apt update && apt install -y --no-install-recommends linux-image-amd64 live-boot systemd-sysv network-manager net-tools wireless-tools curl openssh-client procps vim-tiny libfl-dev cmake ninja-build libfreetype6-dev libinput-dev git autoconf automake texinfo flex bison build-essential unzip zip less libtool mtools gcc-multilib libncurses-dev libgif-dev libjpeg-dev libicns-dev libopenexr-dev libpng-dev libtiff-dev libwebp-dev zlib1g-dev &&\
apt install -y --reinstall ca-certificates &&\
git clone https://github.com/VitruvianOS/Core.git --branch development &&\
cd /Core &&\
sed -i -e 's/Debug //g' configure &&\
mkdir /Core/generated.x86 &&\
cd /Core/generated.x86 &&\
../configure && ninja -j$((`nproc`+1)) &&\
cd /Core/generated.x86/ &&\
cpack &&\
apt -y remove --purge autoconf automake bison build-essential cmake flex gcc-multilib git less libtool mtools ninja-build texinfo unzip zip &&\
apt -y remove --purge *-dev &&\
xargs -a /Core/build/build_deps.txt apt -y remove --purge &&\
apt -y autoremove &&\
apt clean &&\
apt install -y -f /Core/generated.x86/*.deb &&\
rm -rf /Core/ &&\

passwd; exit"

echo ${bold}Create Directories for Live Environment Files...
echo ${normal}

mkdir -p $HOME/LIVE_BOOT/scratch
mkdir -p $HOME/LIVE_BOOT/image/live

echo ${bold}Chroot Environment Compression...
echo ${normal}

sudo mksquashfs \
    $HOME/LIVE_BOOT/chroot \
    $HOME/LIVE_BOOT/image/live/filesystem.squashfs \
    -b 1048576 -comp xz -Xdict-size 100% -e boot

echo ${bold}Copy Kernel and Initramfs from Chroot to Live Directory...
echo ${normal}

cp $HOME/LIVE_BOOT/chroot/boot/vmlinuz-* $HOME/LIVE_BOOT/image/vmlinuz
cp $HOME/LIVE_BOOT/chroot/boot/initrd.img-* $HOME/LIVE_BOOT/image/initrd

echo ${bold}Create Grub Menu...
echo ${normal}

cat <<'EOF' >$HOME/LIVE_BOOT/scratch/grub.cfg
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

touch $HOME/LIVE_BOOT/image/VITRUVIAN_CUSTOM

echo ${bold}GRUB cfg...
echo ${normal}

grub-mkstandalone \
    --format=x86_64-efi \
    --output=$HOME/LIVE_BOOT/scratch/bootx64.efi \
    --locales="" \
    --fonts="" \
    "boot/grub/grub.cfg=$HOME/LIVE_BOOT/scratch/grub.cfg"

echo ${bold}FAT16 Efiboot...
echo ${normal}

cd $HOME/LIVE_BOOT/scratch
dd if=/dev/zero of=efiboot.img bs=1M count=10
mkfs.vfat efiboot.img
mmd -i efiboot.img efi efi/boot
mcopy -i efiboot.img ./bootx64.efi ::efi/boot/

echo ${bold}Grub cfg Modules...
echo ${normal}

grub-mkstandalone \
    --format=i386-pc \
    --output=$HOME/LIVE_BOOT/scratch/core.img \
    --install-modules="linux normal iso9660 biosdisk memdisk search tar ls" \
    --modules="linux normal iso9660 biosdisk search" \
    --locales="" \
    --fonts="" \
    "boot/grub/grub.cfg=$HOME/LIVE_BOOT/scratch/grub.cfg"

cat \
    /usr/lib/grub/i386-pc/cdboot.img \
    $HOME/LIVE_BOOT/scratch/core.img \
> $HOME/LIVE_BOOT/scratch/bios.img

echo ${bold}Generate ISO File...
echo ${normal}

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
    -append_partition 2 0xef ${HOME}/LIVE_BOOT/scratch/efiboot.img \
    -output "${HOME}/LIVE_BOOT/vitruvian-custom.iso" \
    -graft-points \
        "${HOME}/LIVE_BOOT/image" \
        /boot/grub/bios.img=$HOME/LIVE_BOOT/scratch/bios.img \
        /EFI/efiboot.img=$HOME/LIVE_BOOT/scratch/efiboot.img

echo ${bold}Finished!
