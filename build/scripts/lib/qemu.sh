#!/bin/sh

_resolve_disk() {
    _rd_path="$1"

    if [ -b "$_rd_path" ]; then
        log_warn "Attaching real host block device: $_rd_path"
        log_warn "The guest can READ AND WRITE this device — data loss is possible."
        _resolved_disk="$_rd_path"
    elif [ -d "$_rd_path" ]; then
        die "expected a disk image or block device, got a directory: $_rd_path (use --shared-folder for a host folder)"
    elif [ -f "$_rd_path" ]; then
        _resolved_disk="$_rd_path"
    else
        die "disk not found: $_rd_path (create it first, e.g. qemu-img create -f raw $_rd_path 16G)"
    fi
}

run_qemu() {
    _basedir="$1"
    _arch="$2"
    _image_type="$3"
    _console_log="${4:-0}"
    _console_stdout="${5:-0}"
    _shared_folder="${6:-}"
    _usb_disk="${7:-}"
    _target_disk="${8:-}"
    _disk_image="${9:-}"
    _qemu_cmd="$(arch_to_qemu "$_arch")"

    require_cmd "$_qemu_cmd" "qemu-system for $_arch"

    _extra_drives=""
    if [ -n "$_shared_folder" ]; then
        [ -d "$_shared_folder" ] || die "shared folder not found: $_shared_folder"
        _extra_drives="$_extra_drives -drive file=fat:rw:$_shared_folder,format=raw,if=virtio"
    fi
    if [ -n "$_target_disk" ]; then
        _resolve_disk "$_target_disk"
        _extra_drives="$_extra_drives -drive file=$_resolved_disk,format=raw,if=virtio,cache=writethrough"
    fi
    if [ -n "$_usb_disk" ]; then
        if [ -d "$_usb_disk" ]; then
            _usb_file="fat:rw:$_usb_disk"
        else
            _resolve_disk "$_usb_disk"
            _usb_file="$_resolved_disk"
        fi
        _extra_drives="$_extra_drives -device qemu-xhci,id=xhci -drive if=none,id=usbdisk,file=$_usb_file,format=raw -device usb-storage,bus=xhci.0,drive=usbdisk,removable=on"
    fi

    _logfile="$_basedir/vitruvian-console.log"
    if [ "$_console_stdout" -eq 1 ] && [ "$_console_log" -eq 1 ]; then
        _serial_args="-chardev stdio,id=ch0,mux=on,signal=off,logfile=$_logfile -serial chardev:ch0"
    elif [ "$_console_stdout" -eq 1 ]; then
        _serial_args="-serial mon:stdio"
    else
        _serial_args="-serial file:$_logfile"
    fi

    case "$_image_type" in
        disk)
            _resolve_disk "$_disk_image"
            [ -f "$_resolved_disk" ] || [ -b "$_resolved_disk" ] \
                || die "disk image not found: $_disk_image"
            [ "$_arch" = "amd64" ] || die "--image-type=disk is amd64/UEFI only (got $_arch)"

            [ -f /usr/share/OVMF/OVMF_VARS_4M.fd ] \
                || die "OVMF_VARS_4M.fd not found. Install package 'ovmf'."
            _disk_vars="$_basedir/OVMF_VARS.disk.fd"
            cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$_disk_vars"

            log_step "Booting disk image $_resolved_disk in QEMU ($_qemu_cmd)..."
            "$_qemu_cmd" \
                -m 4096 -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
                -drive file="$_resolved_disk",format=raw,if=virtio,cache=writethrough \
                -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
                -drive if=pflash,format=raw,file="$_disk_vars" \
                -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                -device virtio-net-pci,netdev=mynet \
                -device intel-hda -device hda-duplex,audiodev=snd0 -audiodev pa,id=snd0 \
                $_extra_drives \
                $_serial_args
            ;;
        raw)
            _raw="$_basedir/output/vitruvian.raw"
            [ -f "$_raw" ] || die "RAW image not found: $_raw"
            _host_shared="$_basedir/shared"
            mkdir -p "$_host_shared"

            log_step "Booting RAW image in QEMU ($_qemu_cmd)..."
            case "$_arch" in
                amd64)
                    "$_qemu_cmd" \
                        -m 4096 -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
                        -drive file="$_raw",format=raw,if=virtio,cache=writethrough \
                        -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
                        -drive if=pflash,format=raw,file="$_basedir/OVMF_VARS.fd" \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        -device intel-hda -device hda-duplex,audiodev=snd0 -audiodev pa,id=snd0 \
                        -virtfs local,path="$_host_shared",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared \
                        $_extra_drives \
                        $_serial_args
                    ;;
                arm64)
                    "$_qemu_cmd" \
                        -m 2048 -smp 2 \
                        -machine virt \
                        -cpu cortex-a72 \
                        -drive file="$_raw",format=raw,if=virtio,cache=writethrough \
                        -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        -virtfs local,path="$_host_shared",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared \
                        $_extra_drives
                    ;;
                arm32)
                    "$_qemu_cmd" \
                        -m 1024 -smp 1 \
                        -machine virt \
                        -cpu cortex-a15 \
                        -drive file="$_raw",format=raw,if=virtio \
                        -bios /usr/share/qemu-efi-arm/QEMU_EFI.fd
                    ;;
                riscv64)
                    "$_qemu_cmd" \
                        -m 2048 -smp 2 \
                        -machine virt \
                        -drive file="$_raw",format=raw,if=virtio \
                        $_extra_drives
                    ;;
            esac
            ;;
        iso)
            _iso="$_basedir/output/vitruvian-custom.iso"
            [ -f "$_iso" ] || die "ISO image not found: $_iso"

            log_step "Booting ISO in QEMU ($_qemu_cmd)..."
            case "$_arch" in
                amd64)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot order=dc,menu=on \
                        -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        -device intel-hda -device hda-duplex,audiodev=snd0 -audiodev pa,id=snd0 \
                        $_extra_drives \
                        $_serial_args
                    ;;
                arm64)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot order=dc,menu=on \
                        -m 4G -machine virt -cpu cortex-a72 \
                        -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        $_extra_drives \
                        -serial mon:stdio
                    ;;
                arm32)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot order=dc,menu=on \
                        -m 2G -machine virt -cpu cortex-a15 \
                        -bios /usr/share/qemu-efi-arm/QEMU_EFI.fd \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet
                    ;;
                riscv64)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot order=dc,menu=on \
                        -m 4G -machine virt \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        $_extra_drives
                    ;;
            esac
            ;;
        raspberry)
            _raw="$_basedir/output/vitruvian-raspberry.raw"
            [ -f "$_raw" ] || die "Raspberry Pi image not found: $_raw"

            log_step "Booting Raspberry Pi image in QEMU (aarch64)..."
            require_cmd qemu-system-aarch64 "qemu-efi-aarch64"
            qemu-system-aarch64 \
                -m 2048 -smp 4 \
                -machine raspi4b \
                -cpu cortex-a72 \
                -drive file="$_raw",format=raw,if=sd \
                -serial stdio \
                -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                -device virtio-net-pci,netdev=mynet
            ;;
        rpi-arm32)
            _raw="$_basedir/output/vitruvian-rpi-arm32.raw"
            [ -f "$_raw" ] || die "RPi arm32 image not found: $_raw"

            log_step "Booting Raspberry Pi arm32 image in QEMU..."
            require_cmd qemu-system-arm "qemu-system-arm"
            qemu-system-arm \
                -m 1024 -smp 1 \
                -machine raspi2b \
                -cpu cortex-a7 \
                -drive file="$_raw",format=raw,if=sd \
                -serial stdio \
                -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                -device virtio-net-pci,netdev=mynet
            ;;
        rockchip|allwinner|beagle|nxp|amlogic)
            _raw="$_basedir/output/vitruvian-$_image_type.raw"
            [ -f "$_raw" ] || die "$_image_type image not found: $_raw"

            log_warn "QEMU boot for $_image_type boards is not fully supported."
            log_warn "These images are designed for real hardware."
            log_info "Attempting generic arm64 virt boot..."
            qemu-system-aarch64 \
                -m 2048 -smp 2 \
                -machine virt -cpu cortex-a72 \
                -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
                -drive file="$_raw",format=raw,if=virtio \
                -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                -device virtio-net-pci,netdev=mynet
            ;;
        allwinner-h3|beaglebone)
            _raw="$_basedir/output/vitruvian-$_image_type.raw"
            [ -f "$_raw" ] || die "$_image_type image not found: $_raw"

            log_warn "QEMU boot for $_image_type boards is not fully supported."
            log_warn "These images are designed for real hardware."
            log_info "Attempting generic arm32 virt boot..."
            qemu-system-arm \
                -m 1024 -smp 1 \
                -machine virt -cpu cortex-a15 \
                -drive file="$_raw",format=raw,if=virtio \
                -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                -device virtio-net-pci,netdev=mynet
            ;;
        visionfive2|licheerv)
            _raw="$_basedir/output/vitruvian-$_image_type.raw"
            [ -f "$_raw" ] || die "$_image_type image not found: $_raw"

            log_warn "QEMU boot for $_image_type boards is not fully supported."
            log_warn "These images are designed for real hardware."
            log_info "Attempting generic riscv64 virt boot..."
            qemu-system-riscv64 \
                -m 2048 -smp 2 \
                -machine virt \
                -drive file="$_raw",format=raw,if=virtio \
                -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                -device virtio-net-pci,netdev=mynet
            ;;
        *)
            die "Unknown image type for QEMU: $_image_type"
            ;;
    esac
}
