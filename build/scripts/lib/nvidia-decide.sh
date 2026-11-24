#!/bin/sh
#
# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
#
# nvidia-decide.sh: operator opt-in decision rule for the NVIDIA proprietary
# lane on V\OS (nvidia-kernel-dkms plus nvidia-drm.modeset=1). Never default:
# no image build reads this file and no image ships the modeset drop.
#
# Rule: opt in exactly on amd64 + NVIDIA PCI vendor 10de + device id Turing
# (0x1e00) or newer. Older NVIDIA, other vendors and non-amd64 stay off.
#
# Usage:
#   nvidia-decide.sh selftest          replay the mapping + record cases
#   nvidia-decide.sh decide A V D      pure mapping: arch vendor device-id
#   nvidia-decide.sh check [--record]  inspect this machine; with --record
#                                      write the scheduler.conf machine record
#   nvidia-decide.sh print-plan        print the opt-in command sequence
#   nvidia-decide.sh print-packages    print the Debian package set for the lane
#
# Standalone tool: do NOT source this from bake.sh; nothing in the image
# build reads it. NVIDIA_DECIDE_ARCH / NVIDIA_DECIDE_GPU /
# NVIDIA_DECIDE_PCI_DIR / NVIDIA_DECIDE_STATE_DIR override the live probes
# (harness use only).

set -u

# Turing floor by PCI device id; a single floor covers Ampere, Ada,
# Blackwell and anything newer without enumerating generations.
NVIDIA_MIN_DEVICE_ID=1e00

_decide_from_ids() {
    _arch="$1"
    _vendor="$2"
    _devid="$3"

    case "$_arch" in
        amd64|x86_64) ;;
        *) printf 'none\n'; return ;;
    esac

    case "$_vendor" in
        10de|10DE) ;;
        *) printf 'none\n'; return ;;
    esac

    # strip optional 0x/0X prefix, normalise to lowercase, drop leading zeros
    _devid="${_devid#0x}"
    _devid="${_devid#0X}"
    _devid="$(printf '%s' "$_devid" | tr 'A-F' 'a-f')"
    _devid="$(printf '%s' "$_devid" | sed 's/^0*//')"
    [ -n "$_devid" ] || _devid=0

    if [ "$_devid" = "0" ]; then
        printf 'none\n'
        return
    fi

    # both sides as decimal for a portable numeric compare
    _want=$((0x$NVIDIA_MIN_DEVICE_ID))
    _got=$((0x$_devid))
    if [ "$_got" -ge "$_want" ]; then
        printf 'nvidia-proprietary\n'
    else
        printf 'none\n'
    fi
}

# Keep in sync with get_nvidia_image_packages in build/scripts/lib/packages.sh.
_package_set() {
    printf '%s' "nvidia-kernel-dkms nvidia-driver nvidia-egl-common firmware-nvidia-gsp"
}

_detect_arch() {
    if [ -n "${NVIDIA_DECIDE_ARCH:-}" ]; then
        printf '%s\n' "$NVIDIA_DECIDE_ARCH"
        return
    fi
    uname -m 2>/dev/null || printf 'unknown\n'
}

# Print "vendor:device" for the NVIDIA display GPU we find, reading sysfs
# (no lspci dependency). Optimus layouts enumerate the iGPU first, so scan
# every display device and pick any vendor 10de match; fall back to the
# first display device so check can still report a non-NVIDIA box. Empty
# output when none. NVIDIA_DECIDE_PCI_DIR overrides the sysfs root
# (harness use only).
_detect_gpu() {
    if [ -n "${NVIDIA_DECIDE_GPU:-}" ]; then
        printf '%s\n' "$NVIDIA_DECIDE_GPU"
        return
    fi
    _first=""
    for _d in "${NVIDIA_DECIDE_PCI_DIR:-/sys/bus/pci/devices}"/*; do
        [ -r "$_d/vendor" ] && [ -r "$_d/device" ] || continue
        _class=""
        [ -r "$_d/class" ] && _class="$(cat "$_d/class" 2>/dev/null)"
        # display controllers: 0x03xxxx (vga/3d/display)
        case "$_class" in
            0x03*) ;;
            *) continue ;;
        esac
        _v="$(cat "$_d/vendor" 2>/dev/null)"
        _p="$(cat "$_d/device" 2>/dev/null)"
        [ -n "$_v" ] && [ -n "$_p" ] || continue
        _v="${_v#0x}"
        _p="${_p#0x}"
        [ -n "$_first" ] || _first="$_v:$_p"
        # hybrid laptops: the iGPU above is intel; this is the one to decide on
        case "$_v" in
            10de|10DE)
                printf '%s:%s\n' "$_v" "$_p"
                return
                ;;
        esac
    done
    [ -n "$_first" ] && printf '%s\n' "$_first"
    return 0
}

_state_dir() {
    printf '%s' "${NVIDIA_DECIDE_STATE_DIR:-/var/lib/vos}"
}

_write_record() {
    _gpu="$1"
    _decision="$2"
    _dir="$(_state_dir)"
    mkdir -p "$_dir" || return 1
    _driver="$(modinfo -F version nvidia 2>/dev/null || true)"
    [ -n "$_driver" ] || _driver=uninstalled
    _kernel="$(uname -r 2>/dev/null || printf 'unknown')"
    _stamped="$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || printf 'unknown')"
    {
        printf 'gpu=%s\n' "$_gpu"
        printf 'decision=%s\n' "$_decision"
        printf 'kernel=%s\n' "$_kernel"
        printf 'driver=%s\n' "$_driver"
        printf 'stamped=%s\n' "$_stamped"
    } > "$_dir/scheduler.conf" || return 1
    printf 'recorded %s scheduler.conf (gpu=%s;decision=%s)\n' \
        "$_dir" "$_gpu" "$_decision"
}

_cmd_check() {
    _record=0
    [ "${1:-}" = "--record" ] && _record=1
    _arch="$(_detect_arch)"
    _gpu="$(_detect_gpu)"
    if [ -z "$_gpu" ]; then
        printf 'arch=%s gpu=none-decided decision=none\n' "$_arch"
        printf 'no display-class PCI GPU found; not opting in\n'
        return 1
    fi
    _vendor="${_gpu%%:*}"
    _devid="${_gpu##*:}"
    _decision="$(_decide_from_ids "$_arch" "$_vendor" "$_devid")"
    printf 'arch=%s gpu=%s decision=%s\n' "$_arch" "$_gpu" "$_decision"
    if [ "$_decision" != "nvidia-proprietary" ]; then
        printf 'staying on the open path (no NVIDIA opt-in for this GPU)\n'
        return 1
    fi
    if [ "$_record" -eq 1 ]; then
        _write_record "$_gpu" "$_decision" || return 1
    fi
    printf 'amd64 Turing+ NVIDIA detected; the proprietary lane is opt-in only.\n'
    printf 'Run `nvidia-decide.sh print-plan` for the exact enable sequence.\n'
    return 0
}

_cmd_print_plan() {
    cat <<'EOF'
# V\OS NVIDIA proprietary opt-in plan; run as root on this machine.
# Gate first: this plan is void unless check prints nvidia-proprietary.
nvidia-decide.sh check --record
# Canonical path: vos-nvidia-enable runs every step below, including the
# PREEMPT_RT overrides the dkms stage needs, then installs the kernel hook.
vos-nvidia-enable
# On a PREEMPT_RT kernel the helper refuses unless you add --accept-rt:
# NVIDIA does not support RT; the build is proven but runtime is unknown.
# Manual equivalent (what the helper runs):
EOF
    printf 'apt-get install -y --no-install-recommends %s\n' "$(_package_set)"
    cat <<'EOF'
sed -i 's|^BUILD_EXCLUSIVE_CONFIG=|#&|' /usr/src/nvidia-current-*/dkms.conf
IGNORE_PREEMPT_RT_PRESENCE=1 dkms autoinstall -k "$(uname -r)"
# The KMS drop lands only after the install and the dkms build succeed, so a
# failed step leaves nouveau loadable.
mkdir -p /etc/modprobe.d /etc/vos /etc/kernel/postinst.d /etc/apt/apt.conf.d
printf 'options nvidia-drm modeset=1\nblacklist nouveau\n' > /etc/modprobe.d/nvidia.conf
install -m 755 /usr/libexec/vos-nvidia-dkms-hook /etc/kernel/postinst.d/vos-nvidia-dkms
# Package upgrades ship a fresh dkms.conf; this Post-Invoke rebuilds after
# every dpkg run while the lane is enabled.
printf '%s\n' 'DPkg::Post-Invoke { "[ ! -f /etc/vos/nvidia.conf ] || [ ! -x /usr/libexec/vos-nvidia-dkms-hook ] || /usr/libexec/vos-nvidia-dkms-hook $(uname -r) || true"; };' > /etc/apt/apt.conf.d/90vos-nvidia-dkms
printf 'VOS_NVIDIA_PROPRIETARY=1\n' > /etc/vos/nvidia.conf
update-initramfs -u
update-grub
# reboot; Safe Mode keeps nomodeset (40_vos) so the opt-in stays off there.
# Secure Boot: enroll MOK or disable it; V\OS ships no enrollment flow.
# After reboot: cat /sys/module/nvidia_drm/parameters/modeset  # expect Y
# Revert: vos-nvidia-enable --disable, then reboot.
EOF
}

_cmd_selftest() {
    _pass=0
    _fail=0
    _run_case() {
        _got="$(_decide_from_ids "$1" "$2" "$3")"
        if [ "$_got" = "$4" ]; then
            _pass=$((_pass + 1))
            printf 'ok   %s %s:%s -> %s (%s)\n' "$1" "$2" "$3" "$_got" "$5"
        else
            _fail=$((_fail + 1))
            printf 'FAIL %s %s:%s -> %s, want %s (%s)\n' \
                "$1" "$2" "$3" "$_got" "$4" "$5"
        fi
    }

    # Turing and newer (opt in)
    _run_case amd64 10de 1e04 nvidia-proprietary 'TU102 RTX 2080 Ti'
    _run_case amd64 10de 1e02 nvidia-proprietary 'TU104 RTX 2080'
    _run_case amd64 10de 1e07 nvidia-proprietary 'TU102 TITAN RTX'
    _run_case amd64 10de 1eb1 nvidia-proprietary 'TU106 RTX 2070'
    _run_case amd64 10de 1f08 nvidia-proprietary 'TU116 GTX 1660'
    _run_case amd64 10de 1f14 nvidia-proprietary 'TU117 GTX 1650'
    # Ampere / Ada / newer (opt in)
    _run_case amd64 10de 2204 nvidia-proprietary 'GA102 RTX 3080'
    _run_case amd64 10de 2504 nvidia-proprietary 'GA100 A100'
    _run_case amd64 10de 2684 nvidia-proprietary 'AD102 RTX 4090'
    _run_case amd64 10de 2782 nvidia-proprietary 'AD103 RTX 4080'
    _run_case amd64 10de 27b0 nvidia-proprietary 'Blackwell floor covers newer ids'
    # pre-Turing NVIDIA (stay off)
    _run_case amd64 10de 1b00 none 'GP100 Pascal'
    _run_case amd64 10de 1c03 none 'GP107 GTX 1050 Ti'
    _run_case amd64 10de 13c0 none 'GM204 Maxwell GTX 980'
    # other vendors (stay off)
    _run_case amd64 1002 73bf none 'AMD Navi21'
    _run_case amd64 8086 46a6 none 'Intel Alder Lake GT2'
    # arch gate: same Turing GPU, not amd64 (stay off)
    _run_case arm64  10de 1e04 none 'Turing on arm64 is out of scope'

    # record contract: five keys on opt-in, refusal and no file otherwise
    _scratch="./.nvidia-decide-selftest.$$"
    mkdir -p "$_scratch" || return 1
    trap 'rm -rf "$_scratch"' EXIT INT TERM
    if NVIDIA_DECIDE_ARCH=amd64 NVIDIA_DECIDE_GPU=10de:2204 \
        NVIDIA_DECIDE_STATE_DIR="$_scratch" sh "$0" check --record \
        >/dev/null 2>&1 \
        && [ "$(grep -c '^gpu=\|^decision=\|^kernel=\|^driver=\|^stamped=' \
            "$_scratch/scheduler.conf" 2>/dev/null)" = "5" ] \
        && grep -q '^decision=nvidia-proprietary$' "$_scratch/scheduler.conf"
    then
        _pass=$((_pass + 1))
        printf 'ok   record contract: five keys, decision=nvidia-proprietary\n'
    else
        _fail=$((_fail + 1))
        printf 'FAIL record contract: scheduler.conf missing or short\n'
    fi
    rm -f "$_scratch/scheduler.conf"
    if NVIDIA_DECIDE_ARCH=amd64 NVIDIA_DECIDE_GPU=8086:46a6 \
        NVIDIA_DECIDE_STATE_DIR="$_scratch" sh "$0" check --record \
        >/dev/null 2>&1; then
        _fail=$((_fail + 1))
        printf 'FAIL record refusal: check --record opted in on Intel GPU\n'
    elif [ -f "$_scratch/scheduler.conf" ]; then
        _fail=$((_fail + 1))
        printf 'FAIL record refusal: scheduler.conf written on Intel GPU\n'
    else
        _pass=$((_pass + 1))
        printf 'ok   record refusal: no opt-in, no scheduler.conf on Intel GPU\n'
    fi
    # hybrid scan: the intel iGPU enumerates first; decide on any NVIDIA
    # display device instead of stopping at the first card (Optimus layout).
    _pci="$_scratch/pci-hybrid-turing"
    mkdir -p "$_pci/0000:00:02.0" "$_pci/0000:01:00.0"
    printf '0x030000\n' > "$_pci/0000:00:02.0/class"
    printf '0x8086\n' > "$_pci/0000:00:02.0/vendor"
    printf '0x46a6\n' > "$_pci/0000:00:02.0/device"
    printf '0x030000\n' > "$_pci/0000:01:00.0/class"
    printf '0x10de\n' > "$_pci/0000:01:00.0/vendor"
    printf '0x2204\n' > "$_pci/0000:01:00.0/device"
    _out="$(NVIDIA_DECIDE_ARCH=amd64 NVIDIA_DECIDE_PCI_DIR="$_pci" \
        sh "$0" check 2>/dev/null)"
    if [ "$?" -eq 0 ] && printf '%s\n' "$_out" | \
        grep -q '^arch=amd64 gpu=10de:2204 decision=nvidia-proprietary$'; then
        _pass=$((_pass + 1))
        printf 'ok   hybrid Turing: iGPU first, NVIDIA decided on\n'
    else
        _fail=$((_fail + 1))
        printf 'FAIL hybrid Turing: %s\n' "$_out"
    fi

    _pci="$_scratch/pci-hybrid-old"
    mkdir -p "$_pci/0000:00:02.0" "$_pci/0000:01:00.0"
    printf '0x030000\n' > "$_pci/0000:00:02.0/class"
    printf '0x8086\n' > "$_pci/0000:00:02.0/vendor"
    printf '0x46a6\n' > "$_pci/0000:00:02.0/device"
    printf '0x030000\n' > "$_pci/0000:01:00.0/class"
    printf '0x10de\n' > "$_pci/0000:01:00.0/vendor"
    printf '0x13c0\n' > "$_pci/0000:01:00.0/device"
    _out="$(NVIDIA_DECIDE_ARCH=amd64 NVIDIA_DECIDE_PCI_DIR="$_pci" \
        sh "$0" check 2>/dev/null)"
    if [ "$?" -eq 1 ] && printf '%s\n' "$_out" | \
        grep -q '^arch=amd64 gpu=10de:13c0 decision=none$'; then
        _pass=$((_pass + 1))
        printf 'ok   hybrid pre-Turing: NVIDIA found, decision none\n'
    else
        _fail=$((_fail + 1))
        printf 'FAIL hybrid pre-Turing: %s\n' "$_out"
    fi

    _pci="$_scratch/pci-intel"
    mkdir -p "$_pci/0000:00:02.0"
    printf '0x030000\n' > "$_pci/0000:00:02.0/class"
    printf '0x8086\n' > "$_pci/0000:00:02.0/vendor"
    printf '0x46a6\n' > "$_pci/0000:00:02.0/device"
    _out="$(NVIDIA_DECIDE_ARCH=amd64 NVIDIA_DECIDE_PCI_DIR="$_pci" \
        sh "$0" check 2>/dev/null)"
    if [ "$?" -eq 1 ] && printf '%s\n' "$_out" | \
        grep -q '^arch=amd64 gpu=8086:46a6 decision=none$'; then
        _pass=$((_pass + 1))
        printf 'ok   intel only: reported, decision none\n'
    else
        _fail=$((_fail + 1))
        printf 'FAIL intel only: %s\n' "$_out"
    fi

    mkdir -p "$_scratch/pci-none"
    _out="$(NVIDIA_DECIDE_ARCH=amd64 NVIDIA_DECIDE_PCI_DIR="$_scratch/pci-none" \
        sh "$0" check 2>/dev/null)"
    if [ "$?" -eq 1 ] && printf '%s\n' "$_out" | \
        grep -q '^arch=amd64 gpu=none-decided decision=none$'; then
        _pass=$((_pass + 1))
        printf 'ok   no display GPU: not opted in\n'
    else
        _fail=$((_fail + 1))
        printf 'FAIL no display GPU: %s\n' "$_out"
    fi

    rm -rf "$_scratch"
    trap - EXIT INT TERM

    _total=$((_pass + _fail))
    printf 'selftest: %s/%s cases green\n' "$_pass" "$_total"
    [ "$_fail" -eq 0 ] && [ "$_total" -eq 23 ]
}

case "${1:-}" in
    selftest) _cmd_selftest ;;
    decide)
        if [ "$#" -ne 4 ]; then
            printf 'usage: %s decide <arch> <vendor-hex> <device-hex>\n' "$0" >&2
            exit 2
        fi
        _decide_from_ids "$2" "$3" "$4"
        ;;
    check)
        if [ "$#" -gt 2 ] || { [ "$#" -eq 2 ] && [ "$2" != "--record" ]; }; then
            printf 'usage: %s check [--record]\n' "$0" >&2
            exit 2
        fi
        _cmd_check "${2:-}"
        ;;
    print-plan) _cmd_print_plan ;;
    print-packages) _package_set; printf '\n' ;;
    *)
        printf 'usage: %s {selftest|decide A V D|check [--record]|print-plan|print-packages}\n' "$0" >&2
        exit 2
        ;;
esac
