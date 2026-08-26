#!/bin/sh
set -e

SCRIPT_PATH="$(readlink -f "$0")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"
LIB_DIR="$SCRIPT_DIR/lib"

. "$LIB_DIR/common.sh"
. "$LIB_DIR/packages.sh"
. "$LIB_DIR/boards.sh"
. "$LIB_DIR/chroot.sh"
. "$LIB_DIR/image.sh"
. "$LIB_DIR/qemu.sh"

usage() {
    cat <<'EOF'
Usage: bake <command> [options]

Commands:
  clean                  Run ninja clean
  build                  Build and create image
  boot                   Boot existing image in QEMU (no rebuild)
  create disk            Create a blank raw disk image (install target / USB)
  generate               Create a raw GPT disk with a formatted partition
                          (a ready-to-mount / installable target)

Create options:
  --output=PATH          Where to write the image (required)
  --size=SIZE            Disk size, qemu-img syntax, e.g. 16G (required)

Generate options:
  --ext4                 Format the single GPT partition as ext4 (required)
  --size=SIZE            Disk size, qemu-img syntax (default 8G)
  PATH                   Output image path (positional), e.g.
                          bake generate --ext4 /tmp/target.img

Boot options:
  --image-type=TYPE      Image type to boot (required). Use 'disk' to boot an
                          arbitrary raw disk image as the primary UEFI disk
                          (pair with --disk-image=PATH), e.g. an installed target.
  --disk-image=PATH      With --image-type=disk, the raw disk image to boot as
                          the primary drive (e.g. a disk produced by the Installer).
  --arch=ARCH            Target architecture (reads from buildstate.conf if omitted)
  --shared-folder=DIR    Expose host DIR to the guest as a mountable data volume
                          (QEMU vvfat FAT disk; rw but fragile under heavy writes).
                          This is a data share, NOT an install target.
  --target-disk=PATH     Attach an existing disk image as an install target
                          (make one with `bake create disk`). This is what the
                          Installer sees as a destination volume.
  --usb-disk=PATH        Attach PATH as a removable USB stick. A directory is
                          exposed via vvfat; a file or block device is a raw disk.
  --enable-console-log   Write serial output to vitruvian-console.log
  --enable-console-stdout  Stream serial output to stdio (combine to tee)
                          (shared-folder/usb-disk: amd64, arm64, riscv64)

Build options:
  --image-type=TYPE      Image type(s), comma-separated: raw, iso, raspberry, rpi-arm32,
                          rockchip, allwinner, allwinner-h3, beagle,
                          beaglebone, nxp, amlogic, visionfive2, licheerv
  --run-qemu             Boot image in QEMU after build
  --enable-console-log   Write serial output to vitruvian-console.log
  --enable-console-stdout  Stream serial output to stdio (combine with --enable-console-log to tee)
  --regenerate-chroot    Recreate the chroot before building
  --arch=ARCH            Target architecture: amd64, arm64, arm32, riscv64
                          (reads from buildstate.conf if omitted)
  --list-boards          List available board types
  --help                 Show this help
EOF
    exit 0
}

BASEDIR="$(realpath ./)"

load_buildstate() {
    if [ -f "$BASEDIR/buildstate.conf" ]; then
        . "$BASEDIR/buildstate.conf"
    fi
    ARCH="${ARCH:-amd64}"

    if [ -f "$BASEDIR/CMakeCache.txt" ]; then
        _cached_arch="$(grep -m1 '^VITRUVIAN_TARGET_ARCH:' "$BASEDIR/CMakeCache.txt" | cut -d= -f2)"
        _canon_arch() { case "$1" in x86_64) echo amd64 ;; aarch64) echo arm64 ;; *) echo "$1" ;; esac; }
        if [ -n "$_cached_arch" ] \
                && [ "$(_canon_arch "$_cached_arch")" != "$(_canon_arch "$ARCH")" ]; then
            die "Arch mismatch: buildstate.conf says $ARCH but cmake was configured for $_cached_arch. Remove this generated directory and start fresh."
        fi

        _cached_chroot="$(grep -m1 '^VITRUVIAN_CHROOT_BUILD:' "$BASEDIR/CMakeCache.txt" | cut -d= -f2)"
        if [ -n "$_cached_chroot" ]; then
            case "$_cached_chroot" in
                ON|on|1|TRUE|true|YES|yes|Y|y) _cmake_chroot=1 ;;
                *) _cmake_chroot=0 ;;
            esac
            if [ "${CHROOT_BUILD:-0}" -ne "$_cmake_chroot" ]; then
                die "Chroot mismatch: buildstate.conf says CHROOT_BUILD=${CHROOT_BUILD:-0} but cmake was configured with VITRUVIAN_CHROOT_BUILD=$_cached_chroot. Remove this directory and start again."
            fi
        fi
    fi
}

cmd_clean() {
    require_cmd ninja "ninja-build"
    log_step "Cleaning build..."
    ninja clean
}

cmd_build() {
    _image_type=""
    _run_qemu=0
    _regenerate=0
    _arch=""
    _list_boards=0
    _console_log=0
    _console_stdout=0
    _shared_folder=""
    _usb_disk=""
    _target_disk=""
    _has_chroot=0

    for arg in "$@"; do
        case "$arg" in
            --image-type=*)
                _image_type="${arg#*=}"
                ;;
            --run-qemu)
                _run_qemu=1
                ;;
            --regenerate-chroot)
                _regenerate=1
                ;;
            --arch=*)
                _arch="${arg#*=}"
                ;;
            --list-boards)
                _list_boards=1
                ;;
            --enable-console-log)
                _console_log=1
                ;;
            --enable-console-stdout)
                _console_stdout=1
                ;;
            --shared-folder=*)
                _shared_folder="${arg#*=}"
                ;;
            --usb-disk=*)
                _usb_disk="${arg#*=}"
                ;;
            --target-disk=*)
                _target_disk="${arg#*=}"
                ;;
            --help|-h)
                usage
                ;;
            *)
                die "Unknown option: $arg"
                ;;
        esac
    done

    if [ "$_list_boards" -eq 1 ]; then
        printf '%-15s %s\n' "TYPE" "DESCRIPTION"
        printf '%-15s %s\n' "raw" "Generic EFI raw image (amd64/arm64/riscv64)"
        printf '%-15s %s\n' "iso" "Live ISO image (amd64/arm64/riscv64)"
        for _bt in $(board_list_types); do
            printf '%-15s %s\n' "$_bt" "$(board_config "$_bt" label)"
        done
        exit 0
    fi

    load_buildstate
    if [ -n "$_arch" ]; then
        ARCH="$_arch"
    fi

    if [ -d "$BASEDIR/image_tree/chroot" ]; then
        _has_chroot=1
    fi

    if [ -z "$_image_type" ]; then
        require_cmd ninja "ninja-build"
        if [ "$_regenerate" -eq 1 ]; then
            chroot_regenerate "$BASEDIR" "$ARCH"
        fi
        log_step "Running ninja build..."
        ninja
        log_info "No image type specified - packages built."
        exit 0
    fi

    # Allow comma-separated list, e.g. --image-type=iso,raw
    _types_csv="$(printf '%s' "$_image_type" | tr ',' ' ')"
    _types=""
    _forced_arch=""
    for _t in $_types_csv; do
        case "$_t" in
            raw|iso) _t_arch="" ;;
            raspberry|rockchip|allwinner|beagle|nxp|amlogic) _t_arch="arm64" ;;
            rpi-arm32|allwinner-h3|beaglebone)               _t_arch="arm32" ;;
            visionfive2|licheerv)                            _t_arch="riscv64" ;;
            *) die "Invalid image type: $_t. Use --list-boards to see available types." ;;
        esac
        if [ -n "$_t_arch" ]; then
            if [ -n "$_forced_arch" ] && [ "$_forced_arch" != "$_t_arch" ]; then
                die "Cannot mix image types with different forced architectures ($_forced_arch vs $_t_arch)."
            fi
            _forced_arch="$_t_arch"
        fi
        _types="$_types $_t"
    done
    if [ -n "$_forced_arch" ]; then
        ARCH="$_forced_arch"
    fi

    _count=0
    for _t in $_types; do _count=$((_count + 1)); done
    if [ "$_run_qemu" -eq 1 ] && [ "$_count" -gt 1 ]; then
        die "--run-qemu requires a single --image-type."
    fi

    # Check for chroot at the beginning
    if [ "$_has_chroot" -eq 0 ] && [ "$_regenerate" -eq 0 ]; then
        die "Image build requires a chroot at $BASEDIR/image_tree/chroot. Reconfigure with --chroot-build, or pass --regenerate-chroot."
    fi

    require_cmd ninja "ninja-build"

    if [ "$_regenerate" -eq 1 ]; then
        chroot_regenerate "$BASEDIR" "$ARCH"
        _has_chroot=1
    fi

    if ! sudo -v; then
        die "sudo authentication required for image creation."
    fi
    ( while true; do sleep 60; sudo -n true 2>/dev/null || exit; kill -0 "$$" 2>/dev/null || exit; done ) &
    _sudo_keepalive_pid=$!
    trap 'kill "$_sudo_keepalive_pid" 2>/dev/null || true' EXIT INT TERM

    log_step "Running ninja build..."
    ninja

    log_step "Packaging debs..."
    cpack

    for _t in $_types; do
        case "$_t" in
            raw)
                create_raw "$BASEDIR" "$ARCH"
                ;;
            iso)
                create_iso "$BASEDIR" "$ARCH"
                ;;
            raspberry|rpi-arm32)
                create_raspberry "$BASEDIR" "$_t"
                ;;
            rockchip|allwinner|allwinner-h3|beagle|beaglebone|nxp|amlogic|visionfive2|licheerv)
                create_uboot_board "$BASEDIR" "$_t"
                ;;
        esac
    done

    if [ "$_run_qemu" -eq 1 ]; then
        # exactly one type at this point
        for _t in $_types; do run_qemu "$BASEDIR" "$ARCH" "$_t" "$_console_log" "$_console_stdout" "$_shared_folder" "$_usb_disk" "$_target_disk"; done
    fi
}

cmd_boot() {
    _image_type=""
    _arch=""
    _console_log=0
    _console_stdout=0
    _shared_folder=""
    _usb_disk=""
    _target_disk=""
    _disk_image=""

    for arg in "$@"; do
        case "$arg" in
            --image-type=*)
                _image_type="${arg#*=}"
                ;;
            --disk-image=*)
                _disk_image="${arg#*=}"
                ;;
            --arch=*)
                _arch="${arg#*=}"
                ;;
            --enable-console-log)
                _console_log=1
                ;;
            --enable-console-stdout)
                _console_stdout=1
                ;;
            --shared-folder=*)
                _shared_folder="${arg#*=}"
                ;;
            --usb-disk=*)
                _usb_disk="${arg#*=}"
                ;;
            --target-disk=*)
                _target_disk="${arg#*=}"
                ;;
            --help|-h)
                usage
                ;;
            *)
                die "Unknown option: $arg"
                ;;
        esac
    done

    load_buildstate
    if [ -n "$_arch" ]; then
        ARCH="$_arch"
    fi

    if [ -z "$_image_type" ]; then
        die "Missing --image-type. Specify what to boot (raw, iso, raspberry, ...)"
    fi

    case "$_image_type" in
        raw|iso) ;;
        disk)
            [ -n "$_disk_image" ] || die "--image-type=disk requires --disk-image=PATH"
            ;;
        raspberry|rockchip|allwinner|beagle|nxp|amlogic)
            ARCH="arm64"
            ;;
        rpi-arm32|allwinner-h3|beaglebone)
            ARCH="arm32"
            ;;
        visionfive2|licheerv)
            ARCH="riscv64"
            ;;
        *)
            die "Unknown image type: $_image_type"
            ;;
    esac

    run_qemu "$BASEDIR" "$ARCH" "$_image_type" "$_console_log" "$_console_stdout" \
        "$_shared_folder" "$_usb_disk" "$_target_disk" "$_disk_image"
}

cmd_create() {
    _what="${1:-}"
    [ -n "$_what" ] && shift
    case "$_what" in
        disk) ;;
        ""|--help|-h) die "Usage: bake create disk --output=PATH --size=SIZE" ;;
        *) die "Unknown create target: $_what (only 'disk' is supported)" ;;
    esac

    _output=""
    _size=""
    for arg in "$@"; do
        case "$arg" in
            --output=*) _output="${arg#*=}" ;;
            --size=*)   _size="${arg#*=}" ;;
            --help|-h)  die "Usage: bake create disk --output=PATH --size=SIZE" ;;
            *)          die "Unknown option: $arg" ;;
        esac
    done

    [ -n "$_output" ] || die "Missing --output=PATH"
    [ -n "$_size" ] || die "Missing --size=SIZE (e.g. --size=16G)"
    [ -e "$_output" ] && die "refusing to overwrite existing file: $_output"

    require_cmd qemu-img "qemu-utils"
    log_step "Creating blank $_size disk image: $_output"
    qemu-img create -f raw "$_output" "$_size" >/dev/null \
        || die "failed to create disk image: $_output"
    log_info "Attach it with: bake boot --image-type=iso --target-disk=$_output"
}

cmd_generate() {
    _output=""
    _size="8G"
    _fs=""
    for arg in "$@"; do
        case "$arg" in
            --ext4)     _fs="ext4" ;;
            --size=*)   _size="${arg#*=}" ;;
            --output=*) _output="${arg#*=}" ;;
            --help|-h)  die "Usage: bake generate --ext4 [--size=SIZE] /path/to/disk.img" ;;
            --*)        die "Unknown option: $arg" ;;
            *)
                [ -z "$_output" ] || die "Multiple output paths given: $_output and $arg"
                _output="$arg"
                ;;
        esac
    done

    [ -n "$_output" ] || die "Missing output path. Usage: bake generate --ext4 [--size=SIZE] /path/to/disk.img"
    [ -n "$_fs" ] || die "Missing filesystem. Only --ext4 is supported."
    [ -e "$_output" ] && die "refusing to overwrite existing file: $_output"

    PATH="/usr/sbin:/sbin:$PATH"
    require_cmd truncate coreutils
    require_cmd sfdisk util-linux
    require_cmd dd coreutils
    require_cmd "mkfs.ext4" e2fsprogs
    require_cmd "mkfs.vfat" dosfstools

    log_step "Creating raw GPT disk with bios_grub + ESP + ext4 root ($_size): $_output"

    _bios_start_sectors=2048
    _bios_size_sectors=2048
    _esp_start_sectors=$((_bios_start_sectors + _bios_size_sectors))
    _esp_size_sectors=1048576
    _root_start_sectors=$((_esp_start_sectors + _esp_size_sectors))
    _esp_offset_bytes=$((_esp_start_sectors * 512))
    _root_offset_bytes=$((_root_start_sectors * 512))

    _bios_type=21686148-6449-6E6F-744E-656564454649

    truncate -s "$_size" "$_output" || die "failed to create disk image: $_output"

    if ! printf 'label: gpt\nstart=%s, size=%s, type=%s, name="vitruvian-bios"\nstart=%s, size=%s, type=uefi, name="vitruvian-esp"\nstart=%s, type=linux, name="vitruvian-data"\n' \
            "$_bios_start_sectors" "$_bios_size_sectors" "$_bios_type" \
            "$_esp_start_sectors" "$_esp_size_sectors" \
            "$_root_start_sectors" \
            | sfdisk --quiet "$_output"; then
        rm -f "$_output"
        die "sfdisk failed to write the GPT partition table"
    fi

    _root_sectors="$(sfdisk -d "$_output" 2>/dev/null | awk '
        /name="vitruvian-data"/ && match($0, /size=[ ]*[0-9]+/) {
            s = substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", s); print s; exit
        }')"
    [ -n "$_root_sectors" ] && [ "$_root_sectors" -gt 0 ] 2>/dev/null \
        || { rm -f "$_output"; die "could not read root partition size from sfdisk"; }

    _root_blocks=$((_root_sectors / 8))

    _esp_tmp="$(mktemp "${_output}.esp.XXXXXX")" \
        || die "could not create temp file for ESP"
    trap 'rm -f "$_esp_tmp"' EXIT
    truncate -s "$((_esp_size_sectors * 512))" "$_esp_tmp" \
        || { die "failed to size ESP temp file"; }
    if ! mkfs.vfat -F32 -n VITRUVIAN "$_esp_tmp" >/dev/null; then
        rm -f "$_output" "$_esp_tmp"
        die "mkfs.vfat failed"
    fi
    if ! dd if="$_esp_tmp" of="$_output" bs=512 seek="$_esp_start_sectors" \
            conv=notrunc status=none; then
        rm -f "$_output" "$_esp_tmp"
        die "failed to write ESP into disk image"
    fi
    rm -f "$_esp_tmp"
    trap - EXIT

    if ! mkfs.ext4 -F -q -b 4096 -I 512 \
            -O ^ea_inode,^orphan_file,^metadata_csum_seed,^casefold,^encrypt,^verity \
            -L vitruvian-data -E offset="$_root_offset_bytes" "$_output" "$_root_blocks"; then
        rm -f "$_output"
        die "mkfs.ext4 failed"
    fi

    log_info "Created $_output (GPT: 1MiB bios_grub + 512MiB ESP fat32 'VITRUVIAN' + ext4 'vitruvian-data')."
    log_info "Attach it with: bake boot --image-type=iso --target-disk=$_output"
}

[ $# -eq 0 ] && usage

_cmd="$1"
shift

case "$_cmd" in
    clean)
        cmd_clean "$@"
        ;;
    build)
        cmd_build "$@"
        ;;
    boot)
        cmd_boot "$@"
        ;;
    create)
        cmd_create "$@"
        ;;
    generate)
        cmd_generate "$@"
        ;;
    *)
        die "Unknown command: $_cmd (use 'clean', 'build', 'boot', 'create', or 'generate')"
        ;;
esac
