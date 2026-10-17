#!/bin/sh

# testing's RT kernel builds no i915; RT stays on trixie only.
get_kernel_flavour() {
    if [ -n "${VOS_KERNEL_FLAVOUR:-}" ]; then
        printf '%s' "$VOS_KERNEL_FLAVOUR"
    elif [ "${VOS_BASE_SUITE:-trixie}" = "trixie" ]; then
        printf 'rt-amd64'
    else
        printf 'amd64'
    fi
}

get_base_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            printf '%s' \
                "apt-utils dialog linux-image-$(get_kernel_flavour) systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session" \
                " network-manager modemmanager bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " network-manager-openvpn network-manager-l2tp network-manager-openconnect" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults xfsprogs" \
                " fdisk e2fsprogs btrfs-progs cryptsetup dosfstools" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins x42-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " firmware-iwlwifi firmware-atheros firmware-realtek firmware-libertas firmware-brcm80211 firmware-misc-nonfree" \
                " firmware-intel-graphics firmware-amd-graphics firmware-nvidia-graphics firmware-mediatek bluez-firmware" \
                " firmware-sof-signed firmware-intel-sound firmware-cirrus intel-microcode amd64-microcode" \
                " firmware-ti-connectivity firmware-intel-misc firmware-samsung firmware-siano firmware-zd1211 firmware-ath9k-htc firmware-carl9170" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1" \
                " grub-common grub2-common grub-efi-amd64-bin grub-efi-ia32-bin grub-pc-bin"
            ;;
        arm64)
            printf '%s' \
                "apt-utils dialog linux-image-arm64 systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session" \
                " network-manager modemmanager bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " network-manager-openvpn network-manager-l2tp network-manager-openconnect" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults xfsprogs" \
                " fdisk e2fsprogs btrfs-progs cryptsetup dosfstools" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins x42-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1" \
                " grub-common grub2-common grub-efi-arm64-bin"
            ;;
        arm32)
            printf '%s' \
                "apt-utils dialog linux-image-armmp systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session" \
                " network-manager modemmanager bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults" \
                " fdisk e2fsprogs" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1" \
                " grub-common"
            ;;
        riscv64)
            printf '%s' \
                "apt-utils dialog linux-image-riscv64 systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session" \
                " network-manager modemmanager bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults xfsprogs" \
                " fdisk e2fsprogs btrfs-progs cryptsetup dosfstools" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire-audio pipewire-bin wireplumber gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1" \
                " grub-common grub2-common grub-efi-riscv64-bin"
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
                "linux-headers-$(get_kernel_flavour) pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev" \
                " libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev"
            ;;
        arm64)
            printf '%s' \
                "linux-headers-arm64 pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev" \
                " libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev"
            ;;
        arm32)
            printf '%s' \
                "linux-headers-armmp pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev"
            ;;
        riscv64)
            printf '%s' \
                "linux-headers-riscv64 pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev"
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
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-$(get_kernel_flavour) grub-common grub2-common" \
                " grub-efi-amd64-bin grub-efi-ia32-bin grub-pc-bin xfsprogs"
            ;;
        arm64)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 grub-common grub2-common grub-efi-arm64 grub-efi-arm64-bin xfsprogs"
            ;;
        arm32)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-armmp xfsprogs"
            ;;
        riscv64)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 grub-common grub2-common grub-efi-riscv64 grub-efi-riscv64-bin xfsprogs"
            ;;
        *)
            die "No raw image package list for architecture: $_arch"
            ;;
    esac
}

# Board images get the same system as raw/ISO images minus GRUB, plus the board's own boot
# and firmware packages from _board_extra_packages.
get_board_packages() {
    _board="$1"
    case "$_board" in
        raspberry|rockchip|allwinner|beagle|nxp|amlogic) _bp_arch=arm64 ;;
        rpi-arm32|allwinner-h3|beaglebone) _bp_arch=arm32 ;;
        visionfive2|licheerv) _bp_arch=riscv64 ;;
        *) die "No board package list for: $_board" ;;
    esac
    get_base_packages "$_bp_arch" | tr ' ' '\n' | grep -v '^grub' | tr '\n' ' '
    _board_extra_packages "$_board"
}

_board_extra_packages() {
    _board="$1"
    case "$_board" in
        raspberry)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 linux-headers-arm64 raspi-firmware dosfstools rsync firmware-brcm80211 bluez-firmware"
            ;;
        rpi-arm32)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-armmp linux-headers-armmp raspi-firmware dosfstools rsync firmware-brcm80211 bluez-firmware"
            ;;
        rockchip)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 linux-headers-arm64 u-boot-rockchip dosfstools rsync"
            ;;
        allwinner)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 linux-headers-arm64 u-boot-sunxi dosfstools rsync"
            ;;
        allwinner-h3)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-armmp u-boot-sunxi dosfstools rsync"
            ;;
        beagle)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 u-boot-sitara-binaries dosfstools rsync"
            ;;
        beaglebone)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-armmp u-boot-beaglebone dosfstools rsync"
            ;;
        nxp)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 dosfstools rsync"  # u-boot-imx is armhf-only
            ;;
        amlogic)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 linux-headers-arm64 dosfstools rsync"
            # No Debian Amlogic U-Boot since bookworm; fip.sh builds the blob.
            # linux-headers-arm64 is load-bearing: nexus-dkms' preinst
            # interlock refuses to unpack without a headers tree under
            # /usr/src (2026-10-17 amlogic leg 20261017T171747Z-3395292).
            ;;
        visionfive2)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 linux-headers-riscv64 u-boot-starfive dosfstools rsync"
            ;;
        licheerv)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-riscv64 linux-headers-riscv64 dosfstools rsync"
            ;;
        *)
            die "No board package list for: $_board"
            ;;
    esac
}
