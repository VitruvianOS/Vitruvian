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

Create options:
  --output=PATH          Where to write the image (required)
  --size=SIZE            Disk size, qemu-img syntax, e.g. 16G (required)

Boot options:
  --image-type=TYPE      Image type to boot (required)
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
        if [ -n "$_cached_arch" ] && [ "$_cached_arch" != "$ARCH" ]; then
            die "Arch mismatch: buildstate.conf says $ARCH but cmake was configured for $_cached_arch. Remove this generated directory and start fresh."
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

    _has_chroot=0
    [ -d "$BASEDIR/image_tree/chroot" ] && _has_chroot=1

    require_cmd ninja "ninja-build"

    if [ "$_regenerate" -eq 1 ]; then
        chroot_regenerate "$BASEDIR" "$ARCH"
        _has_chroot=1
    fi

    # Image creation calls sudo many times from deep inside subshells,
    # where a mid-run password prompt can eat Ctrl+C. Prime the sudo
    # timestamp upfront so all later calls hit the cache silently. If
    # the user aborts (Ctrl+C at the prompt), we exit cleanly here.
    if ! sudo -v; then
        die "sudo authentication required for image creation."
    fi
    # Keep the sudo timestamp fresh during long builds so it doesn't
    # expire mid-flight and re-prompt. Killed when the parent exits.
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
                [ "$_has_chroot" -eq 0 ] && die "Raw image requires a chroot. Run setupenv with --chroot-build first."
                create_raw "$BASEDIR" "$ARCH"
                ;;
            iso)
                [ "$_has_chroot" -eq 0 ] && die "ISO image requires a chroot. Run setupenv with --chroot-build first."
                create_iso "$BASEDIR" "$ARCH"
                ;;
            raspberry|rpi-arm32)
                [ "$_has_chroot" -eq 0 ] && die "Raspberry image requires a chroot. Run setupenv with --chroot-build first."
                create_raspberry "$BASEDIR" "$_t"
                ;;
            rockchip|allwinner|allwinner-h3|beagle|beaglebone|nxp|amlogic|visionfive2|licheerv)
                [ "$_has_chroot" -eq 0 ] && die "Board image requires a chroot. Run setupenv with --chroot-build first."
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

    for arg in "$@"; do
        case "$arg" in
            --image-type=*)
                _image_type="${arg#*=}"
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
        "$_shared_folder" "$_usb_disk" "$_target_disk"
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
    *)
        die "Unknown command: $_cmd (use 'clean', 'build', 'boot', or 'create')"
        ;;
esac
