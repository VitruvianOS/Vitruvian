#!/bin/sh

run_qemu() {
    _basedir="$1"
    _arch="$2"
    _image_type="$3"
    _console_log="${4:-0}"
    _console_stdout="${5:-0}"
    _qemu_cmd="$(arch_to_qemu "$_arch")"

    require_cmd "$_qemu_cmd" "qemu-system for $_arch"

    # Default (no flags): write serial to vitruvian-console.log (original).
    # --enable-console-stdout alone: serial to stdio, no file.
    # Both flags: tee stdio + file via logfile= chardev.
    _logfile="$_basedir/vitruvian-console.log"
    if [ "$_console_stdout" -eq 1 ] && [ "$_console_log" -eq 1 ]; then
        _serial_args="-chardev stdio,id=ch0,mux=on,signal=off,logfile=$_logfile -serial chardev:ch0"
    elif [ "$_console_stdout" -eq 1 ]; then
        _serial_args="-serial mon:stdio"
    else
        _serial_args="-serial file:$_logfile"
    fi

    case "$_image_type" in
        raw)
            _raw="$_basedir/output/vitruvian.raw"
            [ -f "$_raw" ] || die "RAW image not found: $_raw"
            _host_shared="$_basedir/shared"
            mkdir -p "$_host_shared"

            log_step "Booting RAW image in QEMU ($_qemu_cmd)..."
            case "$_arch" in
                amd64)
                    # cache=writethrough: writes hit the backing file in
                    # order so closing the qemu window (kill -9) never loses
                    # data the guest thinks was flushed. Slightly slower
                    # than the writeback default, but stops the
                    # "reboot/close breaks the image" class of bug.
                    "$_qemu_cmd" \
                        -m 4096 -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
                        -drive file="$_raw",format=raw,if=virtio,cache=writethrough \
                        -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
                        -drive if=pflash,format=raw,file="$_basedir/OVMF_VARS.fd" \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        -virtfs local,path="$_host_shared",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared \
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
                        -virtfs local,path="$_host_shared",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared
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
                        -drive file="$_raw",format=raw,if=virtio
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
                        -cdrom "$_iso" -boot menu=on \
                        -m 8G -cpu host -smp sockets=1,cores=2,threads=2 --enable-kvm \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        $_serial_args
                    ;;
                arm64)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot menu=on \
                        -m 4G -machine virt -cpu cortex-a72 \
                        -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet \
                        -serial mon:stdio
                    ;;
                arm32)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot menu=on \
                        -m 2G -machine virt -cpu cortex-a15 \
                        -bios /usr/share/qemu-efi-arm/QEMU_EFI.fd \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet
                    ;;
                riscv64)
                    "$_qemu_cmd" \
                        -cdrom "$_iso" -boot menu=on \
                        -m 4G -machine virt \
                        -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
                        -device virtio-net-pci,netdev=mynet
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
