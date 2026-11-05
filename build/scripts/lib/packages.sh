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

# Kernel image metapackage the image installs; the vos deb depends on it.
get_kernel_package() {
    _arch="$1"
    case "$_arch" in
        amd64)     printf 'linux-image-%s' "$(get_kernel_flavour)" ;;
        arm64)     printf 'linux-image-arm64' ;;
        arm|arm32) printf 'linux-image-armmp' ;;
        riscv64)   printf 'linux-image-riscv64' ;;
        *)         die "No kernel image package for architecture: $_arch" ;;
    esac
}

# Kernel images a system may carry. amd64 lists both flavours (trixie RT,
# testing generic): the deb is packed before the suite is known.
get_kernel_packages() {
    _arch="$1"
    get_kernel_package "$_arch"
    if [ "$_arch" != amd64 ]; then
        return 0
    fi
    _first="$(get_kernel_package amd64)"
    for _f in rt-amd64 amd64; do
        _p="linux-image-$_f"
        if [ "$_p" != "$_first" ]; then
            printf ' %s' "$_p"
        fi
    done
}

# Debian renamed the VP8 runtime in testing: libvpx9 on trixie, libvpx12 after.
get_libvpx_runtime() {
    if [ "${VOS_BASE_SUITE:-trixie}" = "trixie" ]; then
        printf '%s' 'libvpx9'
    else
        printf '%s' 'libvpx12'
    fi
}

get_base_packages() {
    _arch="$1"
    case "$_arch" in
        amd64)
            printf '%s' \
                "apt-utils dialog $(get_kernel_package amd64) systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session seatd zstd" \
                " network-manager modemmanager libmm-glib0 libre1 $(get_libvpx_runtime) mobile-broadband-provider-info bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " network-manager-openvpn network-manager-l2tp network-manager-openconnect" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults xfsprogs" \
                " fdisk e2fsprogs btrfs-progs cryptsetup dosfstools" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire pipewire-alsa pipewire-bin wireplumber rtkit libspa-0.2-bluetooth gstreamer1.0-pipewire gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav" \
                " alsa-ucm-conf alsa-topology-conf gstreamer1.0-gl mesa-va-drivers" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins x42-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb libnss-mdns" \
                " firmware-iwlwifi firmware-atheros firmware-realtek firmware-libertas firmware-brcm80211 firmware-misc-nonfree" \
                " firmware-intel-graphics firmware-amd-graphics firmware-nvidia-graphics firmware-mediatek bluez-firmware" \
                " firmware-sof-signed firmware-intel-sound firmware-cirrus intel-microcode amd64-microcode" \
                " firmware-ti-connectivity firmware-intel-misc firmware-samsung firmware-siano firmware-zd1211 firmware-ath9k-htc firmware-carl9170" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1 libetpan20t64 libcurl3t64-gnutls" \
                " gpsd" \
                " grub-common grub2-common grub-efi-amd64-bin grub-efi-ia32-bin grub-pc-bin hwdata"
            ;;
        arm64)
            printf '%s' \
                "apt-utils dialog $(get_kernel_package arm64) systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session seatd zstd" \
                " network-manager modemmanager libmm-glib0 libre1 $(get_libvpx_runtime) mobile-broadband-provider-info eg25-manager bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " network-manager-openvpn network-manager-l2tp network-manager-openconnect" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults xfsprogs" \
                " fdisk e2fsprogs btrfs-progs cryptsetup dosfstools" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire pipewire-alsa pipewire-bin wireplumber rtkit libspa-0.2-bluetooth gstreamer1.0-pipewire gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav" \
                " alsa-ucm-conf alsa-topology-conf gstreamer1.0-gl mesa-va-drivers libgl1-mesa-dri" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins x42-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb libnss-mdns" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1 libetpan20t64 libcurl3t64-gnutls" \
                " firmware-brcm80211 firmware-misc-nonfree firmware-realtek firmware-atheros firmware-libertas bluez-firmware" \
                " gpsd" \
                " grub-common grub2-common grub-efi-arm64-bin hwdata"
            ;;
        arm32)
            printf '%s' \
                "apt-utils dialog $(get_kernel_package arm32) systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session seatd zstd" \
                " network-manager modemmanager libmm-glib0 libre1 $(get_libvpx_runtime) mobile-broadband-provider-info bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults" \
                " fdisk e2fsprogs" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire pipewire-alsa pipewire-bin wireplumber rtkit libspa-0.2-bluetooth gstreamer1.0-pipewire gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav" \
                " alsa-ucm-conf alsa-topology-conf gstreamer1.0-gl mesa-va-drivers" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb libnss-mdns" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1 libetpan20t64 libcurl3t64-gnutls" \
                " gpsd" \
                " grub-common hwdata"
            ;;
        riscv64)
            printf '%s' \
                "apt-utils dialog $(get_kernel_package riscv64) systemd-sysv systemd-timesyncd" \
                " polkitd pkexec sudo dbus-user-session seatd zstd" \
                " network-manager modemmanager libmm-glib0 libre1 $(get_libvpx_runtime) mobile-broadband-provider-info bluez bluez-obexd net-tools wireless-tools wireless-regdb wpasupplicant rfkill curl openssh-client" \
                " procps vim-tiny libbinutils openssh-server locales libnss-myhostname xdg-user-dirs ca-certificates iputils-ping linux-sysctl-defaults xfsprogs" \
                " fdisk e2fsprogs btrfs-progs cryptsetup dosfstools" \
                " fortune-mod ncurses-bin rsync" \
                " pipewire pipewire-alsa pipewire-bin wireplumber rtkit libspa-0.2-bluetooth gstreamer1.0-pipewire gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav" \
                " alsa-ucm-conf alsa-topology-conf gstreamer1.0-gl mesa-va-drivers" \
                " libcamera-ipa libspa-0.2-libcamera libcamera-v4l2 v4l-utils" \
                " swh-plugins frei0r-plugins" \
                " cups cups-filters printer-driver-gutenprint printer-driver-cups-pdf" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb libnss-mdns" \
                " libwpewebkit-2.0-1 libwpe-1.0-1 libwpebackend-fdo-1.0-1 libetpan20t64 libcurl3t64-gnutls" \
                " gpsd" \
                " grub-common grub2-common grub-efi-riscv64-bin hwdata"
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
                " libevdev-dev libseat-dev libudev-dev libusb-1.0-0-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev libavif-dev libssl-dev libargon2-dev" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev libsane-dev liblilv-dev libsuil-dev lv2-dev libasound2-dev" \
                " libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev libetpan-dev libcurl4-gnutls-dev libre-dev libvpx-dev"
            ;;
        arm64)
            printf '%s' \
                "linux-headers-arm64 pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev libusb-1.0-0-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev libavif-dev libssl-dev libargon2-dev" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev libsane-dev liblilv-dev libsuil-dev lv2-dev libasound2-dev" \
                " libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev libetpan-dev libcurl4-gnutls-dev libre-dev libvpx-dev"
            ;;
        arm32)
            printf '%s' \
                "linux-headers-armmp pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev libusb-1.0-0-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev libavif-dev libssl-dev libargon2-dev" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev libsane-dev liblilv-dev libsuil-dev lv2-dev libasound2-dev libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev libetpan-dev libcurl4-gnutls-dev libre-dev libvpx-dev"
            ;;
        riscv64)
            printf '%s' \
                "linux-headers-riscv64 pkg-config libc6-dev libcrypt-dev libstdc++-14-dev" \
                " libfreetype-dev libicu-dev libcairo2-dev libcups2-dev libdrm-dev libinput-dev" \
                " libevdev-dev libseat-dev libudev-dev libusb-1.0-0-dev zlib1g-dev libgif-dev" \
                " libblkid-dev libbacktrace-dev libfl-dev libncurses-dev" \
                " libgl-dev libegl-dev libgbm-dev" \
                " libxkbcommon-dev libsystemd-dev libpam0g-dev libpwquality-dev" \
                " libgcrypt20-dev libapt-pkg-dev" \
                " libjpeg-dev libpng-dev libtiff-dev libwebp-dev libicns-dev libavif-dev libssl-dev libargon2-dev" \
                " libsane1 libsane-common sane-utils sane-airscan ipp-usb" \
                " libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libfluidsynth-dev libsane-dev liblilv-dev libsuil-dev lv2-dev libasound2-dev libglu1-mesa-dev libnm-dev libbluetooth-dev" \
                " libwpewebkit-2.0-dev libwpe-1.0-dev libwpebackend-fdo-1.0-dev libetpan-dev libcurl4-gnutls-dev libre-dev libvpx-dev"
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
                " $(get_kernel_package amd64) grub-common grub2-common" \
                " grub-efi-amd64-bin grub-efi-ia32-bin grub-pc-bin xfsprogs"
            ;;
        arm64)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " $(get_kernel_package arm64) grub-common grub2-common grub-efi-arm64 grub-efi-arm64-bin xfsprogs"
            ;;
        arm32)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " $(get_kernel_package arm32) xfsprogs"
            ;;
        riscv64)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " $(get_kernel_package riscv64) grub-common grub2-common grub-efi-riscv64 grub-efi-riscv64-bin xfsprogs"
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
                " linux-image-arm64 linux-headers-arm64 u-boot-rockchip dosfstools rsync firmware-brcm80211 bluez-firmware"
            ;;
        allwinner)
            printf '%s' \
                "systemd systemd-sysv dbus-user-session polkitd pkexec sudo accountsservice libpam-pwquality libpwquality-tools libpwquality-dev systemd-timesyncd locales console-setup keyboard-configuration xdg-user-dirs ca-certificates iputils-ping vim net-tools iproute2 openssh-server" \
                " linux-image-arm64 linux-headers-arm64 u-boot-sunxi dosfstools rsync libgl1-mesa-dri"
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
