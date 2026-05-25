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

Boot options:
  --image-type=TYPE      Image type to boot (required)
  --arch=ARCH            Target architecture (reads from buildstate.conf if omitted)

Build options:
  --image-type=TYPE      Image type(s), comma-separated: raw, iso, raspberry, rpi-arm32,
                          rockchip, allwinner, allwinner-h3, beagle,
                          beaglebone, nxp, amlogic, visionfive2, licheerv
  --run-qemu             Boot image in QEMU after build
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
        for _t in $_types; do run_qemu "$BASEDIR" "$ARCH" "$_t"; done
    fi
}

cmd_boot() {
    _image_type=""
    _arch=""

    for arg in "$@"; do
        case "$arg" in
            --image-type=*)
                _image_type="${arg#*=}"
                ;;
            --arch=*)
                _arch="${arg#*=}"
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

    run_qemu "$BASEDIR" "$ARCH" "$_image_type"
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
    *)
        die "Unknown command: $_cmd (use 'clean', 'build', or 'boot')"
        ;;
esac
