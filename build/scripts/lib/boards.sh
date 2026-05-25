#!/bin/sh

board_is_type() {
    _type="$1"
    case "$_type" in
        efi-generic|raspberry|rpi-arm32|rockchip|allwinner|allwinner-h3|\
        beagle|beaglebone|nxp|amlogic|visionfive2|licheerv)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

board_config() {
    _type="$1"
    _field="$2"
    case "$_type" in
        efi-generic)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'Generic arm64 EFI' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'efi' ;;
                boot_size_mb)   printf '513' ;;
                root_fs)        printf 'xfs' ;;
                bootloader)     printf 'grub' ;;
                extra_pkgs)     printf 'grub-efi-arm64 grub-efi-arm64-bin' ;;
            esac
            ;;
        raspberry)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'Raspberry Pi 4/5' ;;
                partition_fmt)  printf 'dos' ;;
                boot_style)     printf 'rpi-firmware' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'rpi-bootloader' ;;
                extra_pkgs)     printf 'raspberrypi-bootloader libraspberrypi-bin' ;;
                dtb_files)      printf 'broadcom/bcm2711-rpi-4-b.dtb broadcom/bcm2712-rpi-5-b.dtb' ;;
            esac
            ;;
        rpi-arm32)
            case "$_field" in
                arch)           printf 'arm32' ;;
                label)          printf 'Raspberry Pi 1/2/Zero' ;;
                partition_fmt)  printf 'dos' ;;
                boot_style)     printf 'rpi-firmware' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'rpi-bootloader' ;;
                extra_pkgs)     printf 'raspberrypi-bootloader libraspberrypi-bin' ;;
                dtb_files)      printf 'broadcom/bcm2708-rpi-zero.dtb broadcom/bcm2709-rpi-2-b.dtb broadcom/bcm2710-rpi-3-b.dtb' ;;
            esac
            ;;
        rockchip)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'Rockchip (RK3399/RK356x/RK3588)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '64' ;;
                uboot_offset_sectors) printf '16384' ;;
                extra_pkgs)     printf 'u-boot-rockchip' ;;
                dtb_files)      printf 'rockchip/rk3399-rock-pi-4b.dtb rockchip/rk3588-rock-5b.dtb rockchip/rk3566-orangepi-3b.dtb' ;;
            esac
            ;;
        allwinner)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'Allwinner (H6/H616/A64)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '16' ;;
                uboot_offset_sectors) printf '65536' ;;
                extra_pkgs)     printf 'u-boot-sunxi' ;;
                dtb_files)      printf 'allwinner/sun50i-h6-orangepi-3.dtb allwinner/sun50i-h616-orangepi-zero2.dtb allwinner/sun50i-a64-pine64.dtb' ;;
            esac
            ;;
        allwinner-h3)
            case "$_field" in
                arch)           printf 'arm32' ;;
                label)          printf 'Allwinner H2+/H3 (Orange Pi Zero/R1/One/PC)' ;;
                partition_fmt)  printf 'dos' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '16' ;;
                uboot_offset_sectors) printf '65536' ;;
                extra_pkgs)     printf 'u-boot-sunxi' ;;
                dtb_files)      printf 'sun8i-h2-plus-orangepi-zero.dtb sun8i-h3-orangepi-one.dtb sun8i-h3-orangepi-pc.dtb' ;;
            esac
            ;;
        beagle)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'BeagleBoard (AI/X15/Play)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '1' ;;
                uboot_offset_sectors) printf '65536' ;;
                extra_pkgs)     printf 'u-boot-beagle' ;;
                dtb_files)      printf 'ti/k3/am625-beagleplay.dtb' ;;
            esac
            ;;
        beaglebone)
            case "$_field" in
                arch)           printf 'arm32' ;;
                label)          printf 'BeagleBone Black' ;;
                partition_fmt)  printf 'dos' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '1' ;;
                uboot_offset_sectors) printf '65536' ;;
                extra_pkgs)     printf 'u-boot-beaglebone' ;;
                dtb_files)      printf 'am335x-boneblack.dtb am335x-bonegreen.dtb' ;;
            esac
            ;;
        nxp)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'NXP i.MX (6/7/8/9)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '64' ;;
                uboot_offset_sectors) printf '16384' ;;
                extra_pkgs)     printf 'u-boot-imx' ;;
                dtb_files)      printf 'nxp/imx/imx8mq-librem5-devkit.dtb nxp/imx/imx8mp-venice-gw74xx.dtb' ;;
            esac
            ;;
        amlogic)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'Amlogic (G12B/SM1/A311D)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '1' ;;
                uboot_offset_sectors) printf '65536' ;;
                extra_pkgs)     printf 'u-boot-meson' ;;
                dtb_files)      printf 'amlogic/meson-g12b-odroid-n2.dtb amlogic/meson-sm1-khadas-vim3l.dtb amlogic/meson-a1-ad401.dtb' ;;
            esac
            ;;
        visionfive2)
            case "$_field" in
                arch)           printf 'riscv64' ;;
                label)          printf 'StarFive VisionFive 2 (JH7110)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '4096' ;;
                uboot_offset_sectors) printf '16384' ;;
                extra_pkgs)     printf '' ;;
                dtb_files)      printf 'starfive/jh7110-starfive-visionfive-2-v1.3b.dtb' ;;
            esac
            ;;
        licheerv)
            case "$_field" in
                arch)           printf 'riscv64' ;;
                label)          printf 'Sipeed LicheeRV (D1)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                spl_offset_sectors) printf '1' ;;
                uboot_offset_sectors) printf '32768' ;;
                extra_pkgs)     printf '' ;;
                dtb_files)      printf 'allwinner/sun20i-d1-lichee-rv.dtb' ;;
            esac
            ;;
    esac
}

board_list_types() {
    printf '%s\n' efi-generic raspberry rpi-arm32 rockchip allwinner allwinner-h3 \
                  beagle beaglebone nxp amlogic visionfive2 licheerv
}
