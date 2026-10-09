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
                # Debian u-boot-rockchip has no RK356x/RK3588 variant; claim
                # only the board the default (rock-pi-4-rk3399) boots.
                label)          printf 'Rockchip (RK3399 Rock Pi 4)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                # Verified present in the built rootfs 2026-09-14.
                uboot_variant)  printf 'rock-pi-4-rk3399' ;;
                spl_blob)       printf 'idbloader.img' ;;
                uboot_blob)     printf 'u-boot.itb' ;;
                spl_offset_sectors) printf '64' ;;
                uboot_offset_sectors) printf '16384' ;;
                extra_pkgs)     printf 'u-boot-rockchip' ;;
                # U-Boot's fdtfile defaults to the 4A DTB; name the 4B one.
                boot_dtb)       printf 'rockchip/rk3399-rock-pi-4b.dtb' ;;
                # Orange Pi 3B and Rock 5B have no Debian U-Boot; add them
                # back when u-boot-rockchip gains a variant.
                dtb_files)      printf 'rockchip/rk3399-rock-pi-4b.dtb' ;;
            esac
            ;;
        allwinner)
            case "$_field" in
                arch)           printf 'arm64' ;;
                # Only the A64 board the default u-boot-sunxi variant
                # (pine64_plus) boots; H6/H616 need variants not flashed.
                label)          printf 'Allwinner (A64 Pine64)' ;;
                # The SPL goes at sector 16, inside a GPT entry array; MBR
                # has nothing there (as on allwinner-h3).
                partition_fmt)  printf 'dos' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                # Variant list verified against u-boot-sunxi 2025.01-3+deb13u1;
                # pine64_plus matches this board's sun50i-a64-pine64.dtb.
                uboot_variant)  printf 'pine64_plus' ;;
                # sunxi loads one combined blob at 8KiB; there is no separate
                # u-boot stage, and sector 65536 falls inside the boot partition.
                spl_blob)       printf 'u-boot-sunxi-with-spl.bin' ;;
                uboot_blob)     printf '' ;;
                spl_offset_sectors) printf '16' ;;
                uboot_offset_sectors) printf '0' ;;
                extra_pkgs)     printf 'u-boot-sunxi' ;;
                # The DTBs use UART0 at 115200. No boot_dtb: U-Boot picks one
                # through fdtfile.
                console)        printf 'ttyS0,115200' ;;
                # pine64_plus U-Boot asks for the -plus DTB by name; ship both.
                dtb_files)      printf 'allwinner/sun50i-a64-pine64.dtb allwinner/sun50i-a64-pine64-plus.dtb' ;;
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
                # sunxi loads one combined blob at 8KiB; no separate stage.
                spl_blob)       printf 'u-boot-sunxi-with-spl.bin' ;;
                uboot_blob)     printf '' ;;
                spl_offset_sectors) printf '16' ;;
                uboot_offset_sectors) printf '0' ;;
                extra_pkgs)     printf 'u-boot-sunxi' ;;
                dtb_files)      printf 'sun8i-h2-plus-orangepi-zero.dtb sun8i-h3-orangepi-one.dtb sun8i-h3-orangepi-pc.dtb' ;;
            esac
            ;;
        beagle)
            case "$_field" in
                arch)           printf 'arm64' ;;
                label)          printf 'BeaglePlay (AM62x)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                # POSIX case executes the FIRST matching arm only; a second
                # uboot_variant assignment is dead code. Keep one value.
                # u-boot-sitara-binaries splits the k3 chain across two
                # variants: am62x_evm_r5 and am62x_evm_a53.
                uboot_variant)  printf 'am62x_evm_r5 am62x_evm_a53' ;;
                spl_offset_sectors) printf '1' ;;
                uboot_offset_sectors) printf '65536' ;;
                spl_blob)       printf 'tiboot3.bin' ;;
                uboot_blob)     printf 'u-boot.img' ;;
                extra_pkgs)     printf 'u-boot-sitara-binaries' ;;
                # DTB console is serial2; eMMC is mmc0, so the SD image's root
                # is mmcblk1p2.
                console)        printf 'ttyS2,115200' ;;
                rootdev)        printf '/dev/mmcblk1p2' ;;
                boot_dtb)       printf 'ti/k3/am625-beagleplay.dtb' ;;
                # AM62x SD BootROM FS mode loads tiboot3/tispl/u-boot.img
                # as FAT files on the boot partition, not raw sectors.
                uboot_fat_files) printf '1' ;;
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
                # Only i.MX8M parts are arm64; i.MX6/7 are 32-bit and the
                # Debian DTB set for this image is imx8mq/imx8mp only.
                label)          printf 'NXP i.MX8M (8MQ/8MP)' ;;
                partition_fmt)  printf 'gpt' ;;
                boot_style)     printf 'spl-uboot' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                # No arm64 Debian U-Boot here; flash.bin comes from firmware/nxp/.
                # The BootROM reads it at 0x8400 (sector 66).
                uboot_variant)  printf '' ;;
                spl_blob)       printf 'flash.bin' ;;
                uboot_blob)     printf '' ;;
                spl_offset_sectors) printf '66' ;;
                uboot_offset_sectors) printf '0' ;;
                extra_pkgs)     printf '' ;;
                # Debian ships these under freescale/, not nxp/imx/.
                dtb_files)      printf 'freescale/imx8mq-librem5-devkit.dtb freescale/imx8mp-venice-gw74xx.dtb' ;;
                # Librem 5 / Venice debug UART is UART1 (serial0/ttymxc0).
                console)        printf 'ttyS0,115200' ;;
            esac
            ;;
        amlogic)
            case "$_field" in
                arch)           printf 'arm64' ;;
                # The FIP is built for ODROID-N2 only.
                label)          printf 'Amlogic (ODROID-N2)' ;;
                # The FIP is written from sector 1, over where GPT lives;
                # Amlogic SD images keep an MBR.
                partition_fmt)  printf 'msdos' ;;
                boot_style)     printf 'spl-uboot' ;;
                # uart_AO_A at 115200, per the DTB and Hardkernel U-Boot.
                console)        printf 'ttyS0,115200' ;;
                boot_size_mb)   printf '256' ;;
                root_fs)        printf 'ext4' ;;
                bootloader)     printf 'u-boot' ;;
                # Mainline u-boot only; the G12B BootROM requires the vendor
                # FIP-signed blob, assembled at build time by fip.sh from
                # hardkernel/u-boot (travis/odroidn2-189, 430749ab).
                uboot_variant)  printf 'odroid-n2' ;;
                # What Hardkernel's fip/Makefile produces and sd_fusing.sh
                # writes at sector 1.
                spl_blob)       printf 'u-boot.bin' ;;
                uboot_blob)     printf '' ;;
                spl_offset_sectors) printf '1' ;;
                uboot_offset_sectors) printf '0' ;;
                # No u-boot-amlogic(-binaries) in Debian trixie/testing;
                # fip_assemble supplies the blob.
                extra_pkgs)     printf '' ;;
                fip_assemble)   printf '1' ;;
                boot_dtb)       printf 'amlogic/meson-g12b-odroid-n2.dtb' ;;
                # Only the N2 DTB: VIM3L (SM1) and A1 need other FIPs.
                dtb_files)      printf 'amlogic/meson-g12b-odroid-n2.dtb' ;;
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
                # JH7110 boot chain, in the sectors the BootROM scans.
                uboot_variant)  printf 'starfive_visionfive2' ;;
                spl_blob)       printf 'u-boot-spl.bin.normal.out' ;;
                uboot_blob)     printf 'u-boot.itb' ;;
                spl_offset_sectors) printf '4096' ;;
                uboot_offset_sectors) printf '16384' ;;
                # SD boot finds SPL by partition type and u-boot.itb in p2
                # (u-boot-starfive README); QSPI boot ignores them.
                spl_type_guid)  printf '2E54B353-1271-4842-806F-E436D6AF6985' ;;
                uboot_type_guid) printf 'BC13C2FF-59E6-4262-A352-B275FD6F7172' ;;
                # UART0 at 115200, per the DTB and Debian U-Boot.
                console)        printf 'ttyS0,115200' ;;
                boot_dtb)       printf 'starfive/jh7110-starfive-visionfive-2-v1.3b.dtb' ;;
                extra_pkgs)     printf 'u-boot-starfive' ;;
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
                # The D1 BROM also reads sector 256, clear of the GPT that
                # sector 16 overlaps. fip.sh builds the blob (no Debian one).
                spl_blob)       printf 'u-boot-sunxi-with-spl.bin' ;;
                uboot_blob)     printf '' ;;
                spl_offset_sectors) printf '256' ;;
                uboot_offset_sectors) printf '0' ;;
                extra_pkgs)     printf '' ;;
                fip_assemble)   printf '1' ;;
                # DTB stdout-path serial0:115200n8 (UART0 @ 115200).
                console)        printf 'ttyS0,115200' ;;
                boot_dtb)       printf 'allwinner/sun20i-d1-lichee-rv.dtb' ;;
                dtb_files)      printf 'allwinner/sun20i-d1-lichee-rv.dtb' ;;
            esac
            ;;
    esac
}

board_list_types() {
    printf '%s\n' efi-generic raspberry rpi-arm32 rockchip allwinner allwinner-h3 \
                  beagle beaglebone nxp amlogic visionfive2 licheerv
}
