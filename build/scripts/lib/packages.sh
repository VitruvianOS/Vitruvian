#!/bin/sh

get_base_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            printf '%s' \
                "apt-utils dialog linux-image-rt-amd64 live-boot systemd-sysv" \
                " network-manager net-tools wireless-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales xfsprogs" \
                " fortune-mod ncurses-bin rsync"
            ;;
        arm64)
            printf '%s' \
                "apt-utils dialog linux-image-arm64 live-boot systemd-sysv" \
                " network-manager net-tools wireless-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales xfsprogs" \
                " fortune-mod ncurses-bin rsync"
            ;;
        arm32)
            printf '%s' \
                "apt-utils dialog linux-image-armmp live-boot systemd-sysv" \
                " network-manager net-tools wireless-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales" \
                " fortune-mod ncurses-bin rsync"
            ;;
        riscv64)
            printf '%s' \
                "apt-utils dialog linux-image-riscv64 live-boot systemd-sysv" \
                " network-manager net-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales xfsprogs" \
                " ncurses-bin rsync"
            ;;
        *)
            die "No package list for architecture: $_arch"
            ;;
    esac
}

get_dev_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            printf '%s' \
                "linux-headers-rt-amd64 pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev"
            ;;
        arm64)
            printf '%s' \
                "linux-headers-arm64 pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev"
            ;;
        arm32)
            printf '%s' \
                "linux-headers-armmp pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev"
            ;;
        riscv64)
            printf '%s' \
                "linux-headers-riscv64 pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev"
            ;;
        *)
            die "No dev package list for architecture: $_arch"
            ;;
    esac
}

get_raw_image_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-rt-amd64 grub-efi-amd64 grub-efi-amd64-bin" \
                " grub-efi-amd64-signed shim-signed efibootmgr xfsprogs"
            ;;
        arm64)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 grub-efi-arm64 grub-efi-arm64-bin xfsprogs"
            ;;
        arm32)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-armmp xfsprogs"
            ;;
        riscv64)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 grub-efi-riscv64 grub-efi-riscv64-bin xfsprogs"
            ;;
        *)
            die "No raw image package list for architecture: $_arch"
            ;;
    esac
}

get_board_packages() {
    _board="$1"
    case "$_board" in
        raspberry)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 raspberrypi-bootloader libraspberrypi-bin" \
                " dosfstools rsync"
            ;;
        rpi-arm32)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-armmp raspberrypi-bootloader libraspberrypi-bin" \
                " dosfstools rsync"
            ;;
        rockchip)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 u-boot-rockchip dosfstools rsync"
            ;;
        allwinner)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 u-boot-sunxi dosfstools rsync"
            ;;
        allwinner-h3)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-armmp u-boot-sunxi dosfstools rsync"
            ;;
        beagle)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 u-boot-beagle dosfstools rsync"
            ;;
        beaglebone)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-armmp u-boot-beaglebone dosfstools rsync"
            ;;
        nxp)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 u-boot-imx dosfstools rsync"
            ;;
        amlogic)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 u-boot-meson dosfstools rsync"
            ;;
        visionfive2)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 dosfstools rsync"
            ;;
        licheerv)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 dosfstools rsync"
            ;;
        *)
            die "No board package list for: $_board"
            ;;
    esac
}
