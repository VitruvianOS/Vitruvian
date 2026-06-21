#!/bin/sh

get_base_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            printf '%s' \
                "apt-utils dialog linux-image-rt-amd64 systemd-sysv" \
                " network-manager net-tools wireless-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales xfsprogs" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber" \
                " grub-common grub-efi-amd64-bin grub-pc-bin"
            ;;
        arm64)
            printf '%s' \
                "apt-utils dialog linux-image-arm64 systemd-sysv" \
                " network-manager net-tools wireless-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales xfsprogs" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber" \
                " grub-common grub-efi-arm64-bin"
            ;;
        arm32)
            printf '%s' \
                "apt-utils dialog linux-image-armmp systemd-sysv" \
                " network-manager net-tools wireless-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber" \
                " grub-common"
            ;;
        riscv64)
            printf '%s' \
                "apt-utils dialog linux-image-riscv64 systemd-sysv" \
                " network-manager net-tools curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales xfsprogs" \
                " ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber" \
                " grub-common grub-efi-riscv64-bin"
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
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev"
            ;;
        arm64)
            printf '%s' \
                "linux-headers-arm64 pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev"
            ;;
        arm32)
            printf '%s' \
                "linux-headers-armmp pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev"
            ;;
        riscv64)
            printf '%s' \
                "linux-headers-riscv64 pkg-config libc6-dev libstdc++-14-dev" \
                " libfreetype6-dev libicu-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev"
            ;;
        *)
            die "No dev package list for architecture: $_arch"
            ;;
    esac
}

get_iso_image_packages() {
    _arch="$1"
    case "$_arch" in
        amd64|arm64|arm32|riscv64)
            printf '%s' "live-boot"
            ;;
        *)
            die "No iso image package list for architecture: $_arch"
            ;;
    esac
}

get_raw_image_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            # grub-common + grub2-common provide update-grub / grub-mkstandalone.
            # grub-efi-amd64-bin + grub-pc-bin ship the EFI and BIOS modules we embed
            # via grub-mkstandalone. We intentionally do NOT install the signed grub
            # or shim — Secure Boot is not required for our QEMU build, and signed
            # grub's hard-coded /EFI/debian prefix makes it unsuitable as the
            # /EFI/BOOT/BOOTX64.EFI fallback used by removable-media boot.
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-rt-amd64 grub-common grub2-common grub-efi-amd64" \
                " grub-efi-amd64-bin grub-pc-bin xfsprogs"
            ;;
        arm64)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 grub-common grub2-common grub-efi-arm64 grub-efi-arm64-bin xfsprogs"
            ;;
        arm32)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-armmp xfsprogs"
            ;;
        riscv64)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 grub-common grub2-common grub-efi-riscv64 grub-efi-riscv64-bin xfsprogs"
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
                " linux-image-arm64 raspi-firmware dosfstools rsync"
            ;;
        rpi-arm32)
            printf '%s' \
                "systemd systemd-sysv sudo vim net-tools iproute2 openssh-server" \
                " linux-image-armmp raspi-firmware dosfstools rsync"
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
