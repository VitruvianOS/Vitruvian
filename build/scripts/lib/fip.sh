#!/bin/sh
# Assemble SoC-specific bootloader blobs at build time from pinned upstream
# trees.  Each SoC family gets its own fip_<soc>_assemble() function.
#
# Usage: fip_amlogic_assemble <dest_dir> <cache_dir>
#        fip_licheerv_assemble <dest_dir> <cache_dir>
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

    # 2b. GCC 14 promotes int-conversion (pointer -> integer in initializers)
    #     to a hard error. Measured 2026-09-27 on the odroid-n2 proof builds:
    #     attempts 1-3 all died at common/bootm.c:333/350 ("initialization of
    #     'long long unsigned int' from 'void *'"). The old tree initializes
    #     u64 fields from pointer values on purpose; -Wno-error above does NOT
    #     neutralize GCC 14's promoted diagnostics, so disable the promoted
    #     check explicitly on the KBUILD_CFLAGS line.
    find "$_dir" -maxdepth 1 -name "Makefile" \
        -exec sed -i '/^KBUILD_CFLAGS.*:=/s/:=/:= -Wno-error=int-conversion /' {} \; 2>/dev/null || true

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

# Allwinner D1 (LicheeRV): U-Boot + OpenSBI assembly.
#
# The D1 BROM loads boot0 (eGON.BT0) from sector 16 of the SD card.
# Sources are pinned by SHA, not branch, for reproducibility: smaeul/u-boot
# d1-wip and riscv-software-src/opensbi v1.5.1.

FIP_LICHEERV_UBOOT_REPO="https://github.com/smaeul/u-boot.git"
FIP_LICHEERV_UBOOT_BRANCH="d1-wip"
FIP_LICHEERV_UBOOT_SHA="2e89b706f5c956a70c989cd31665f1429e9a0b48"
FIP_LICHEERV_OPENSBI_REPO="https://github.com/riscv-software-src/opensbi.git"
FIP_LICHEERV_OPENSBI_TAG="v1.5.1"
FIP_LICHEERV_OPENSBI_SHA="43cace6c3671e5172d0df0a8963e552bb04b7b20"
FIP_LICHEERV_DEFCONFIG="lichee_rv_dock_defconfig"
FIP_LICHEERV_CROSS_COMPILE="${FIP_LICHEERV_CROSS_COMPILE:-riscv64-linux-gnu-}"

fip_licheerv_assemble() {
    _dest="${1:?fip_licheerv_assemble: dest_dir required}"
    _cache="${2:?fip_licheerv_assemble: cache_dir required}"
    _output_name="u-boot-sunxi-with-spl.bin"

    if [ -f "$_dest/$_output_name" ]; then
        _fip_log "$_output_name already present at $_dest"
        echo "$_dest/$_output_name"
        return 0
    fi

    # Cache key covers both pinned SHAs: bumping either invalidates the hit.
    _cached="$_cache/${FIP_LICHEERV_UBOOT_SHA}-${FIP_LICHEERV_OPENSBI_SHA}/$_output_name"
    if [ -f "$_cached" ]; then
        _fip_log "Cache hit at $_cached"
        mkdir -p "$_dest"
        cp "$_cached" "$_dest/$_output_name"
        echo "$_dest/$_output_name"
        return 0
    fi

    for _cmd in git make dtc; do
        command -v "$_cmd" >/dev/null 2>&1 \
            || { _fip_die "$_cmd is required but not found."; return 1; }
    done
    if ! command -v "${FIP_LICHEERV_CROSS_COMPILE}gcc" >/dev/null 2>&1; then
        _fip_die "Cross-compiler ${FIP_LICHEERV_CROSS_COMPILE}gcc not found."
        _fip_die "Install gcc-riscv64-linux-gnu."
        return 1
    fi

    # --- Build OpenSBI (fw_dynamic.bin) ---
    _opensbi_dir="$_cache/$FIP_LICHEERV_OPENSBI_SHA"
    _opensbi_work="$_opensbi_dir/.src"
    _opensbi_fw="$_opensbi_work/build/platform/generic/firmware/fw_dynamic.bin"
    if [ -f "$_opensbi_fw" ]; then
        _fip_log "OpenSBI fw_dynamic.bin already built at $_opensbi_fw"
    else
        if [ -d "$_opensbi_work/.git" ]; then
            _fip_log "Using existing OpenSBI clone at $_opensbi_work"
        else
            _fip_log "Cloning riscv-software-src/opensbi (tag $FIP_LICHEERV_OPENSBI_TAG, shallow)..."
            mkdir -p "$_opensbi_dir"
            rm -rf "$_opensbi_work"
            git clone --depth 1 --branch "$FIP_LICHEERV_OPENSBI_TAG" \
                "$FIP_LICHEERV_OPENSBI_REPO" "$_opensbi_work" || {
                _fip_die "OpenSBI clone failed."
                return 1
            }
        fi

        _fip_log "Building OpenSBI (PLATFORM=generic)..."
        if ! make -C "$_opensbi_work" \
                CROSS_COMPILE="$FIP_LICHEERV_CROSS_COMPILE" \
                PLATFORM=generic \
                -j"$(nproc 2>/dev/null || echo 4)" \
                >"$_opensbi_dir/opensbi-build.log" 2>&1; then
            tail -20 "$_opensbi_dir/opensbi-build.log" >&2
            _fip_die "OpenSBI build failed (log: $_opensbi_dir/opensbi-build.log)."
            return 1
        fi
        if [ ! -f "$_opensbi_fw" ]; then
            _fip_die "fw_dynamic.bin not produced by OpenSBI build."
            return 1
        fi
        _fip_log "OpenSBI build complete: $_opensbi_fw"
    fi

    # --- Build U-Boot (SPL + U-Boot proper) ---
    # Sourced into image.sh: _uboot_dir there is the flash source, don't reuse it.
    _lrv_uboot_dir="$_cache/$FIP_LICHEERV_UBOOT_SHA"
    _uboot_work="$_lrv_uboot_dir/.build"
    if [ -d "$_uboot_work/.git" ]; then
        _fip_log "Using existing U-Boot clone at $_uboot_work"
    else
        _fip_log "Cloning smaeul/u-boot (branch $FIP_LICHEERV_UBOOT_BRANCH, shallow)..."
        mkdir -p "$_lrv_uboot_dir"
        rm -rf "$_uboot_work"
        git clone --depth 1 --branch "$FIP_LICHEERV_UBOOT_BRANCH" \
            "$FIP_LICHEERV_UBOOT_REPO" "$_uboot_work" || {
            _fip_die "U-Boot clone failed."
            return 1
        }
    fi

    _fip_log "Configuring U-Boot ($FIP_LICHEERV_DEFCONFIG)..."
    if ! make -C "$_uboot_work" \
            CROSS_COMPILE="$FIP_LICHEERV_CROSS_COMPILE" \
            "$FIP_LICHEERV_DEFCONFIG" \
            >"$_lrv_uboot_dir/fip-defconfig.log" 2>&1; then
        tail -20 "$_lrv_uboot_dir/fip-defconfig.log" >&2
        _fip_die "U-Boot defconfig failed (log: $_lrv_uboot_dir/fip-defconfig.log)."
        return 1
    fi

    _fip_log "Building U-Boot SPL + proper (this may take several minutes)..."
    # NO_PYTHON=1 and explicit targets skip binman (SWIG/Python 3.13 issue
    # on Debian trixie); the FIT image and combined binary are built below.
    if ! make -C "$_uboot_work" \
            CROSS_COMPILE="$FIP_LICHEERV_CROSS_COMPILE" \
            OPENSBI="$_opensbi_fw" \
            NO_PYTHON=1 \
            spl/sunxi-spl.bin u-boot.bin \
            -j"$(nproc 2>/dev/null || echo 4)" \
            >"$_lrv_uboot_dir/fip-build.log" 2>&1; then
        tail -30 "$_lrv_uboot_dir/fip-build.log" >&2
        _fip_die "U-Boot build failed (log: $_lrv_uboot_dir/fip-build.log)."
        return 1
    fi

    # Only spl/sunxi-spl.bin and u-boot.bin were built above (NO_PYTHON=1
    # skips binman), so u-boot.itb never exists yet; build it with mkimage.
    _spl="$_uboot_work/spl/sunxi-spl.bin"
    _itb="$_uboot_work/u-boot.itb"
    if [ ! -f "$_spl" ]; then
        _fip_die "spl/sunxi-spl.bin not produced by the U-Boot build."
        return 1
    fi

    if [ ! -f "$_itb" ]; then
        _fip_build_its "$_uboot_work" "$_opensbi_fw" || {
            _fip_die "Manual FIT image creation failed."
            return 1
        }
        _itb="$_uboot_work/u-boot.itb"
        if [ ! -f "$_itb" ]; then
            _fip_die "u-boot.itb still missing after manual build."
            return 1
        fi
    fi

    # SPL (boot0/DRAM init) + FIT image (OpenSBI + U-Boot + DTB) concatenated.
    _combined="$_uboot_work/$_output_name"
    cat "$_spl" "$_itb" > "$_combined"

    _size=$(wc -c < "$_combined")
    _fip_log "D1 FIP assembly complete: $_combined ($_size bytes)"

    # Verify the eGON.BT0 magic at offset 4 in the SPL header.
    if ! dd if="$_combined" bs=1 skip=4 count=8 2>/dev/null \
            | grep -q "eGON.BT0"; then
        _fip_die "eGON.BT0 signature not found in SPL header, refusing."
        return 1
    fi

    if [ "$_size" -lt 500000 ] || [ "$_size" -gt 5000000 ]; then
        _fip_warn "Unexpected blob size $_size bytes (expected ~900 KiB), continuing."
    fi

    mkdir -p "$(dirname "$_cached")" "$_dest"
    cp "$_combined" "$_cached"
    cp "$_combined" "$_dest/$_output_name"

    _fip_log "Installed $_output_name in $_dest"
    echo "$_dest/$_output_name"
    return 0
}

# Build the FIT image (u-boot.itb) with mkimage; binman is skipped above.
_fip_build_its() {
    _workdir="$1"
    _opensbi_fw="$2"

    _uboot_bin="$_workdir/u-boot.bin"
    _opensbi_dt="$_workdir/arch/riscv/dts/sun20i-d1-lichee-rv-dock.dtb"
    _its="$_workdir/u-boot.its"
    _itb_out="$_workdir/u-boot.itb"

    if [ ! -f "$_uboot_bin" ]; then
        _fip_die "u-boot.bin not found at $_uboot_bin"
        return 1
    fi
    if [ ! -f "$_opensbi_dt" ]; then
        _fip_die "D1 DTB not found at $_opensbi_dt"
        return 1
    fi

    # Read load/entry addresses from .config (hex values like 0x42e00000)
    _text_base=$(grep "^CONFIG_TEXT_BASE=" "$_workdir/.config" | cut -d= -f2)
    _opensbi_load=$(grep "^CONFIG_SPL_OPENSBI_LOAD_ADDR=" "$_workdir/.config" | cut -d= -f2)

    _text_base=${_text_base:-0x42e00000}
    _opensbi_load=${_opensbi_load:-0x40000000}

    # The DTS requires 64-bit load/entry addresses as two 32-bit halves.
    # These are 32-bit addresses on D1, so high word is 0.
    _load_hi="0x00000000"
    _load_lo="$_text_base"
    _opensbi_hi="0x00000000"
    _opensbi_lo="$_opensbi_load"

    cat > "$_its" <<ITSEOF
/dts-v1/;
/ {
    description = "U-Boot FIT image for Allwinner D1 (LicheeRV)";
    images {
        opensbi {
            description = "RISC-V OpenSBI fw_dynamic";
            data = /incbin/("$_opensbi_fw");
            type = "firmware";
            os = "opensbi";
            arch = "riscv";
            compression = "none";
            load = <$_opensbi_hi $_opensbi_lo>;
            entry = <$_opensbi_hi $_opensbi_lo>;
        };
        uboot {
            description = "U-Boot";
            data = /incbin/("$_uboot_bin");
            type = "standalone";
            os = "U-Boot";
            arch = "riscv";
            compression = "none";
            load = <$_load_hi $_load_lo>;
            entry = <$_load_hi $_load_lo>;
        };
        fdt {
            description = "Device Tree";
            data = /incbin/("$_opensbi_dt");
            type = "flat_dt";
            compression = "none";
        };
    };
    configurations {
        default = "standard";
        standard {
            description = "Standard configuration";
            firmware = "opensbi";
            loadables = "uboot";
            fdt = "fdt";
        };
    };
};
ITSEOF

    # Find mkimage from the U-Boot build tree.
    _mkimage="$_workdir/tools/mkimage"
    if [ ! -x "$_mkimage" ]; then
        _fip_die "mkimage not found at $_mkimage"
        return 1
    fi

    "$_mkimage" -f "$_its" "$_itb_out" || {
        _fip_die "mkimage FIT creation failed."
        return 1
    }
    _fip_log "FIT image built manually: $_itb_out"
    return 0
}
