#!/bin/sh
# Assemble the Amlogic FIP bootloader blob (u-boot.bin.sd.bin) at build
# time from the pinned hardkernel/u-boot tree.
#
# Usage: fip_amlogic_assemble <dest_dir> <cache_dir>
# Prints the output path; returns 1 on failure.

FIP_UBOOT_REPO="https://github.com/hardkernel/u-boot.git"
FIP_UBOOT_TAG="travis/odroidn2-189"
FIP_UBOOT_SHA="430749ab5e16218e0bb450cb71c523bc6e875295"
FIP_DEFCONFIG="odroidn2_defconfig"

FIP_CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
# scp_task (bl301) needs a bare-metal ARM32 compiler.
FIP_ARM_CROSS_COMPILE="${FIP_ARM_CROSS_COMPILE:-arm-none-eabi-}"

_fip_log()  { printf '[fip] %s\n' "$*"; }
_fip_warn() { printf '[fip] WARN: %s\n' "$*" >&2; }
_fip_die()  { printf '[fip] ERROR: %s\n' "$*" >&2; return 1; }

# The upstream tree is u-boot 2015.01 and predates GCC 12+; apply the
# minimum needed to build it with Debian trixie toolchains.
_fip_apply_compat_patches() {
    _dir="$1"

    # compiler-gcc{12,13,14}.h shims; the tree only ships gcc3/4.
    for _ver in 12 13 14; do
        _h="$_dir/include/linux/compiler-gcc${_ver}.h"
        if [ ! -f "$_h" ]; then
            cat > "$_h" <<'SHIMEOF'
/* Compatibility shim for GCC >= 12 on the hardkernel 2015.01 tree */
#ifndef __COMPILER_GCC_V_H
#define __COMPILER_GCC_V_H
#include <linux/compiler-gcc4.h>
#endif
SHIMEOF
        fi
    done

    sed -i 's/^\(KBUILD_CFLAGS.*\)-Werror\b/\1-Wno-error/' "$_dir/Makefile" 2>/dev/null || true
    find "$_dir" -name "Makefile" \
        -exec sed -i 's/-Werror\b/-Wno-error/g' {} \; 2>/dev/null || true
    # Newer binutils warns on RWX LOAD segments.
    find "$_dir" -name "Makefile" \
        -exec sed -i 's/--fatal-warnings//g' {} \; 2>/dev/null || true
    find "$_dir" -name "Makefile" \
        -exec sed -i 's/-Wno-error-implicit-function-declaration/-Wno-error=implicit-function-declaration/g' {} \; 2>/dev/null || true

    # The parent exports aarch64-linux-gnu- as CROSS_COMPILE; scp_task
    # must keep its own arm-none-eabi- values.
    _scp_mk="$_dir/arch/arm/cpu/armv8/g12b/firmware/scp_task/Makefile"
    if [ -f "$_scp_mk" ]; then
        sed -i 's/^CROSS_COMPILE=arm-none-eabi-/override CROSS_COMPILE=arm-none-eabi-/' "$_scp_mk"
        sed -i 's/^CPP=\$(CROSS_COMPILE)cpp/override CPP=\$(CROSS_COMPILE)cpp/' "$_scp_mk"
        sed -i 's/^CC=\$(CROSS_COMPILE)gcc/override CC=\$(CROSS_COMPILE)gcc/' "$_scp_mk"
        sed -i 's/^LD=\$(CROSS_COMPILE)ld/override LD=\$(CROSS_COMPILE)ld/' "$_scp_mk"
        sed -i 's/^OBJCOPY=\$(CROSS_COMPILE)objcopy/override OBJCOPY=\$(CROSS_COMPILE)objcopy/' "$_scp_mk"
        sed -i 's/^OBJDUMP=\$(CROSS_COMPILE)objdump/override OBJDUMP=\$(CROSS_COMPILE)objdump/' "$_scp_mk"
    fi
}

fip_amlogic_assemble() {
    _dest="${1:?fip_amlogic_assemble: dest_dir required}"
    _cache="${2:?fip_amlogic_assemble: cache_dir required}"

    if [ -f "$_dest/u-boot.bin.sd.bin" ]; then
        _fip_log "u-boot.bin.sd.bin already present at $_dest"
        echo "$_dest/u-boot.bin.sd.bin"
        return 0
    fi

    _cached="$_cache/$FIP_UBOOT_SHA/sd_fuse/u-boot.bin.sd.bin"
    if [ -f "$_cached" ]; then
        _fip_log "Cache hit at $_cached"
        mkdir -p "$_dest"
        cp "$_cached" "$_dest/u-boot.bin.sd.bin"
        echo "$_dest/u-boot.bin.sd.bin"
        return 0
    fi

    for _cmd in git make; do
        command -v "$_cmd" >/dev/null 2>&1 \
            || { _fip_die "$_cmd is required but not found."; return 1; }
    done
    if ! command -v "${FIP_CROSS_COMPILE}gcc" >/dev/null 2>&1; then
        _fip_die "Cross-compiler ${FIP_CROSS_COMPILE}gcc not found. Install gcc-aarch64-linux-gnu."
        return 1
    fi
    if ! command -v "${FIP_ARM_CROSS_COMPILE}gcc" >/dev/null 2>&1; then
        _fip_die "Cross-compiler ${FIP_ARM_CROSS_COMPILE}gcc not found. Install gcc-arm-none-eabi."
        return 1
    fi

    _workdir="$_cache/$FIP_UBOOT_SHA/.build"
    if [ -d "$_workdir/.git" ]; then
        _fip_log "Using existing clone at $_workdir"
    else
        _fip_log "Cloning hardkernel/u-boot (tag $FIP_UBOOT_TAG, shallow)..."
        mkdir -p "$_cache/$FIP_UBOOT_SHA"
        rm -rf "$_workdir"
        # Clone the tag, not the branch: the tag is the pinned commit.
        git clone --depth 1 --branch "$FIP_UBOOT_TAG" \
            "$FIP_UBOOT_REPO" "$_workdir" || {
            _fip_die "Clone failed."
            return 1
        }
    fi

    _fip_apply_compat_patches "$_workdir"

    _fip_log "Configuring ($FIP_DEFCONFIG) with CROSS_COMPILE=$FIP_CROSS_COMPILE ..."
    # Log to a file: piping through tail would mask make's exit status.
    if ! make -C "$_workdir" CROSS_COMPILE="$FIP_CROSS_COMPILE" \
            "$FIP_DEFCONFIG" >"$_workdir/fip-defconfig.log" 2>&1; then
        tail -20 "$_workdir/fip-defconfig.log" >&2
        _fip_die "defconfig failed (log: $_workdir/fip-defconfig.log)."
        return 1
    fi

    _fip_log "Building u-boot (this may take several minutes)..."
    if ! make -C "$_workdir" CROSS_COMPILE="$FIP_CROSS_COMPILE" \
            -j"$(nproc 2>/dev/null || echo 4)" >"$_workdir/fip-build.log" 2>&1; then
        tail -30 "$_workdir/fip-build.log" >&2
        _fip_die "Build failed (log: $_workdir/fip-build.log)."
        return 1
    fi

    # sd_fuse/u-boot.bin.sd.bin is the complete SD image, flashed whole at
    # sector 1 (Hardkernel sd_fusing convention). u-boot.bin is the payload
    # without the MBR header and does not match that convention: no fallback.
    _output="$_workdir/sd_fuse/u-boot.bin.sd.bin"
    if [ ! -f "$_output" ]; then
        _fip_die "sd_fuse/u-boot.bin.sd.bin not produced by the build."
        return 1
    fi

    _size=$(wc -c < "$_output")
    _fip_log "FIP assembly complete: $_output ($_size bytes)"

    # The AML BootROM magic must sit at offset 0x211 or the image will
    # not load.
    if ! dd if="$_output" bs=1 skip=529 count=3 2>/dev/null | grep -q "AML"; then
        _fip_die "AML BootROM signature not found at offset 0x211, refusing."
        return 1
    fi
    if [ "$_size" -lt 500000 ] || [ "$_size" -gt 5000000 ]; then
        _fip_warn "Unexpected blob size $_size bytes (expected ~1.5 MB), continuing."
    fi

    mkdir -p "$(dirname "$_cached")" "$_dest"
    cp "$_output" "$_cached"
    cp "$_output" "$_dest/u-boot.bin.sd.bin"

    _fip_log "Installed u-boot.bin.sd.bin in $_dest"
    echo "$_dest/u-boot.bin.sd.bin"
    return 0
}
