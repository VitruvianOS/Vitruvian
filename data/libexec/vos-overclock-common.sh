#!/bin/sh
# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
#
# vos-overclock-common.sh: shared functions for the overclock helpers.
# Sourced by vos-set-overclock (pkexec) and vos-overclock-apply (boot).
#
# Every write is clamped to the limits the kernel reports; nothing is
# written that sysfs does not describe. Roots and paths honor the
# VOS_OVERCLOCK_* environment overrides so tests can use a fake tree.

SYSFS="${VOS_OVERCLOCK_SYSFS_ROOT:-/sys}"
BOOT_ROOT="${VOS_OVERCLOCK_BOOT_ROOT:-/boot}"
STATE_DIR="${VOS_OVERCLOCK_STATE_DIR:-/var/lib/vos-overclock}"
CONF_FILE="${VOS_OVERCLOCK_CONF:-/etc/vos/overclock.conf}"
BOOT_COUNT_DEFAULT=3

CPU_ROOT="$SYSFS/devices/system/cpu"
POWERCAP_ROOT="$SYSFS/class/powercap"
RPI_BLOCK_BEGIN="# BEGIN VOS OVERCLOCK"
RPI_BLOCK_END="# END VOS OVERCLOCK"

oc_read() {
    [ -r "$1" ] || return 1
    tr -d '\n' < "$1"
}

oc_write() {
    printf '%s' "$2" > "$1"
}

# Echo the cpufreq policy directories, oldest-compatible fallback first.
oc_policy_dirs() {
    found=0
    for dir in "$CPU_ROOT"/cpufreq/policy*; do
        [ -d "$dir" ] || continue
        echo "$dir"
        found=1
    done
    [ "$found" -eq 1 ] && return 0
    if [ -e "$CPU_ROOT/cpu0/cpufreq/scaling_governor" ]; then
        echo "$CPU_ROOT/cpu0/cpufreq"
    fi
}

oc_cpuinfo_range() {
    # Sets hw_min_khz / hw_max_khz from the first policy.
    hw_min_khz=-1
    hw_max_khz=-1
    for dir in $(oc_policy_dirs); do
        [ -r "$dir/cpuinfo_min_freq" ] || continue
        hw_min_khz="$(oc_read "$dir/cpuinfo_min_freq")"
        hw_max_khz="$(oc_read "$dir/cpuinfo_max_freq")"
        return 0
    done
    return 1
}

# Save a sysfs file's current value once, before the first write.
oc_snapshot() {
    # $1: snapshot name, $2: sysfs path
    name="$1"
    path="$2"
    mkdir -p "$STATE_DIR/defaults"
    [ -e "$STATE_DIR/defaults/$name" ] && return 0
    [ -r "$path" ] || return 1
    oc_read "$path" > "$STATE_DIR/defaults/$name"
}

oc_restore_defaults() {
    if [ -d "$STATE_DIR/defaults" ]; then
        for snap in "$STATE_DIR/defaults"/*; do
            [ -e "$snap" ] || continue
            name="$(basename "$snap")"
            case "$name" in
                governor) target="$(oc_policy_dirs | head -n1)"; target="$target/scaling_governor" ;;
                min_freq) for d in $(oc_policy_dirs); do oc_write "$d/scaling_min_freq" "$(cat "$snap")" || true; done; continue ;;
                max_freq) for d in $(oc_policy_dirs); do oc_write "$d/scaling_max_freq" "$(cat "$snap")" || true; done; continue ;;
                boost) target="$CPU_ROOT/cpufreq/boost" ;;
                no_turbo) target="$CPU_ROOT/intel_pstate/no_turbo" ;;
                power_long) target="$(oc_rapl_zone)/constraint_0_power_limit_uw" ;;
                power_short) target="$(oc_rapl_zone)/constraint_1_power_limit_uw" ;;
                *) continue ;;
            esac
            [ -n "$target" ] && oc_write "$target" "$(cat "$snap")" || true
        done
    fi
    oc_rpi_clear_block
}

oc_rapl_zone() {
    for zone in "$POWERCAP_ROOT"/intel-rapl:*; do
        [ -d "$zone" ] || continue
        name="$(oc_read "$zone/name" || true)"
        case "$name" in
            package-*) echo "$zone"; return 0 ;;
        esac
    done
    return 1
}

# Apply one KEY=VALUE pair now. Values outside hardware limits are
# clamped; unknown keys or unsupported controls fail loudly.
oc_apply_kv() {
    key="${1%%=*}"
    value="${1#*=}"
    [ "$key" != "$1" ] || { echo "not a key=value pair: $1" >&2; return 2; }

    case "$key" in
        governor)
            written=0
            for dir in $(oc_policy_dirs); do
                [ -r "$dir/scaling_available_governors" ] || continue
                case " $(oc_read "$dir/scaling_available_governors") " in
                    *" $value "*) ;;
                    *) continue ;;
                esac
                oc_snapshot governor "$dir/scaling_governor" || true
                oc_write "$dir/scaling_governor" "$value" && written=1
            done
            [ "$written" -eq 1 ] || { echo "governor not available: $value" >&2; return 2; }
            ;;
        min_freq_khz|max_freq_khz)
            case "$value" in
                ''|*[!0-9]*) echo "invalid frequency: $value" >&2; return 2 ;;
            esac
            oc_cpuinfo_range || { echo "no cpufreq on this system" >&2; return 2; }
            [ "$value" -lt "$hw_min_khz" ] && value="$hw_min_khz"
            [ "$value" -gt "$hw_max_khz" ] && value="$hw_max_khz"
            if [ "$key" = min_freq_khz ]; then
                file=scaling_min_freq
                snapname=min_freq
            else
                file=scaling_max_freq
                snapname=max_freq
            fi
            for dir in $(oc_policy_dirs); do
                oc_snapshot "$snapname" "$dir/$file" || true
                oc_write "$dir/$file" "$value" || return 1
            done
            ;;
        boost)
            if [ -e "$CPU_ROOT/cpufreq/boost" ]; then
                oc_snapshot boost "$CPU_ROOT/cpufreq/boost" || true
                case "$value" in
                    1|0) oc_write "$CPU_ROOT/cpufreq/boost" "$value" ;;
                    *) echo "boost takes 0 or 1" >&2; return 2 ;;
                esac
            elif [ -e "$CPU_ROOT/intel_pstate/no_turbo" ]; then
                oc_snapshot no_turbo "$CPU_ROOT/intel_pstate/no_turbo" || true
                case "$value" in
                    1) oc_write "$CPU_ROOT/intel_pstate/no_turbo" 0 ;;
                    0) oc_write "$CPU_ROOT/intel_pstate/no_turbo" 1 ;;
                    *) echo "boost takes 0 or 1" >&2; return 2 ;;
                esac
            else
                echo "no boost control on this system" >&2
                return 2
            fi
            ;;
        epp)
            written=0
            for dir in $(oc_policy_dirs); do
                [ -r "$dir/energy_performance_available_preferences" ] || continue
                case " $(oc_read "$dir/energy_performance_available_preferences") " in
                    *" $value "*) ;;
                    *) continue ;;
                esac
                oc_write "$dir/energy_performance_preference" "$value" && written=1
            done
            [ "$written" -eq 1 ] || { echo "energy preference not available: $value" >&2; return 2; }
            ;;
        power_long_uw|power_short_uw)
            case "$value" in
                ''|*[!0-9]*) echo "invalid power limit: $value" >&2; return 2 ;;
            esac
            zone="$(oc_rapl_zone)" || { echo "no RAPL package zone" >&2; return 2; }
            if [ "$key" = power_long_uw ]; then
                index=0
            else
                index=1
            fi
            max="$(oc_read "$zone/constraint_${index}_max_power_uw" || true)"
            [ -n "$max" ] && [ "$value" -gt "$max" ] && value="$max"
            snapname="$key"
            oc_snapshot "$snapname" "$zone/constraint_${index}_power_limit_uw" || true
            oc_write "$zone/constraint_${index}_power_limit_uw" "$value" || return 1
            ;;
        rpi_arm_freq|rpi_sdram_freq|rpi_gpu_freq)
            case "$value" in
                ''|*[!0-9]*) echo "invalid clock: $value" >&2; return 2 ;;
            esac
            [ "$value" -ge 100 ] && [ "$value" -le 3000 ] \
                || { echo "clock out of sane range: $value" >&2; return 2; }
            name="${key#rpi_}"
            oc_rpi_set_block "$name" "$value"
            ;;
        rpi_over_voltage)
            echo "$value" | grep -Eq '^-?[0-9]+$' \
                || { echo "invalid offset: $value" >&2; return 2; }
            [ "$value" -ge -16 ] && [ "$value" -le 8 ] \
                || { echo "offset out of firmware range: $value" >&2; return 2; }
            oc_rpi_set_block over_voltage "$value"
            ;;
        *)
            echo "unknown setting: $key" >&2
            return 2
            ;;
    esac
}

oc_config_txt() {
    if [ -f "$BOOT_ROOT/firmware/config.txt" ]; then
        echo "$BOOT_ROOT/firmware/config.txt"
    else
        echo "$BOOT_ROOT/config.txt"
    fi
}

# Set (or with an empty value, remove) one key in the managed block.
oc_rpi_set_block() {
    key="$1"
    value="$2"
    conf="$(oc_config_txt)"
    [ -w "$conf" ] || { echo "cannot write $conf" >&2; return 1; }

    tmp="$STATE_DIR/config.txt.$$"
    mkdir -p "$STATE_DIR"
    awk -v begin="$RPI_BLOCK_BEGIN" -v end="$RPI_BLOCK_END" \
        '$0 == begin { skip = 1 } $0 == end { skip = 0; next } !skip { print }' \
        "$conf" > "$tmp"

    if [ -n "$value" ]; then
        # Keep any keys already in the block except the one replaced.
        if [ -f "$STATE_DIR/rpi-block" ]; then
            grep -v "^$key=" "$STATE_DIR/rpi-block" > "$STATE_DIR/rpi-block.new" || true
            mv "$STATE_DIR/rpi-block.new" "$STATE_DIR/rpi-block"
            echo "$key=$value" >> "$STATE_DIR/rpi-block"
        else
            echo "$key=$value" > "$STATE_DIR/rpi-block"
        fi
    elif [ -f "$STATE_DIR/rpi-block" ]; then
        grep -v "^$key=" "$STATE_DIR/rpi-block" > "$STATE_DIR/rpi-block.new" || true
        mv "$STATE_DIR/rpi-block.new" "$STATE_DIR/rpi-block"
    fi

    if [ -s "$STATE_DIR/rpi-block" ]; then
        echo "$RPI_BLOCK_BEGIN" >> "$tmp"
        cat "$STATE_DIR/rpi-block" >> "$tmp"
        echo "$RPI_BLOCK_END" >> "$tmp"
    else
        rm -f "$STATE_DIR/rpi-block"
    fi

    cat "$tmp" > "$conf"
    rm -f "$tmp"
}

oc_rpi_clear_block() {
    conf="$(oc_config_txt)"
    [ -f "$conf" ] || return 0
    tmp="$STATE_DIR/config.txt.$$"
    mkdir -p "$STATE_DIR"
    awk -v begin="$RPI_BLOCK_BEGIN" -v end="$RPI_BLOCK_END" \
        '$0 == begin { skip = 1 } $0 == end { skip = 0; next } !skip { print }' \
        "$conf" > "$tmp"
    cat "$tmp" > "$conf"
    rm -f "$tmp" "$STATE_DIR/rpi-block"
}

oc_conf_write() {
    # Syntax-check everything first so a bad pair never lands half
    # written; hardware clamping happens when the pair is applied.
    for arg in "$@"; do
        oc_conf_check "$arg" || {
            echo "refusing to persist: $arg" >&2
            return 2
        }
    done
    mkdir -p "$(dirname "$CONF_FILE")"
    : > "$CONF_FILE"
    for arg in "$@"; do
        echo "$arg" >> "$CONF_FILE"
    done
}

# Conf validation without writing: same ranges as oc_apply_kv, but the
# hardware may be absent at boot-validate time, so only syntax-check the
# rpi keys and pass through the sysfs keys for the apply step to judge.
oc_conf_check() {
    key="${1%%=*}"
    value="${1#*=}"
    [ "$key" != "$1" ] || return 2
    case "$key" in
        rpi_arm_freq|rpi_sdram_freq|rpi_gpu_freq)
            case "$value" in
                ''|*[!0-9]*) return 2 ;;
            esac
            [ "$value" -ge 100 ] && [ "$value" -le 3000 ]
            ;;
        rpi_over_voltage)
            echo "$value" | grep -Eq '^-?[0-9]+$' || return 2
            [ "$value" -ge -16 ] && [ "$value" -le 8 ]
            ;;
        governor|min_freq_khz|max_freq_khz|boost|epp|power_long_uw|power_short_uw)
            return 0
            ;;
        *)
            return 2
            ;;
    esac
}

oc_conf_apply() {
    [ -f "$CONF_FILE" ] || return 0
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        case "$line" in \#*) continue ;; esac
        oc_apply_kv "$line" || echo "overclock: skipping $line" >&2
    done < "$CONF_FILE"
}

oc_boot_count_read() {
    if [ -f "$STATE_DIR/boot-count" ]; then
        cat "$STATE_DIR/boot-count"
    else
        echo "$BOOT_COUNT_DEFAULT"
    fi
}

oc_boot_count_set() {
    mkdir -p "$STATE_DIR"
    echo "$1" > "$STATE_DIR/boot-count"
}

oc_enable_units() {
    if command -v systemctl >/dev/null 2>&1; then
        systemctl enable vos-overclock.service vos-overclock-ok.service >/dev/null 2>&1 || true
    fi
}

oc_disable_units() {
    if command -v systemctl >/dev/null 2>&1; then
        systemctl disable --now vos-overclock.service vos-overclock-ok.service >/dev/null 2>&1 || true
    fi
}

# One hex line per SPD EEPROM: spd_<slot>_hex=...
oc_spd() {
    slot=0
    found=0
    for eeprom in "$SYSFS"/bus/i2c/devices/*-005[0-3]/eeprom; do
        [ -r "$eeprom" ] || continue
        hex="$(od -An -v -tx1 "$eeprom" | tr -d ' \n')"
        [ -n "$hex" ] || continue
        echo "spd_${slot}_hex=$hex"
        slot=$((slot + 1))
        found=1
        [ "$slot" -ge 4 ] && break
    done
    [ "$found" -eq 1 ]
}

oc_status() {
    for dir in $(oc_policy_dirs); do
        echo "governor=$(oc_read "$dir/scaling_governor" || true)"
        echo "governors=$(oc_read "$dir/scaling_available_governors" || true)"
        echo "min_freq_khz=$(oc_read "$dir/scaling_min_freq" || true)"
        echo "max_freq_khz=$(oc_read "$dir/scaling_max_freq" || true)"
        echo "hw_min_freq_khz=$(oc_read "$dir/cpuinfo_min_freq" || true)"
        echo "hw_max_freq_khz=$(oc_read "$dir/cpuinfo_max_freq" || true)"
        echo "cur_freq_khz=$(oc_read "$dir/scaling_cur_freq" || true)"
        break
    done
    if [ -e "$CPU_ROOT/cpufreq/boost" ]; then
        echo "boost_available=1"
        echo "boost=$(oc_read "$CPU_ROOT/cpufreq/boost" || true)"
    elif [ -e "$CPU_ROOT/intel_pstate/no_turbo" ]; then
        echo "boost_available=1"
        no_turbo="$(oc_read "$CPU_ROOT/intel_pstate/no_turbo" || true)"
        [ "$no_turbo" = "0" ] && echo "boost=1" || echo "boost=0"
    else
        echo "boost_available=0"
    fi
    for dir in $(oc_policy_dirs); do
        if [ -r "$dir/energy_performance_preference" ]; then
            echo "epp=$(oc_read "$dir/energy_performance_preference" || true)"
            echo "epp_list=$(oc_read "$dir/energy_performance_available_preferences" || true)"
        fi
        break
    done
    zone="$(oc_rapl_zone || true)"
    if [ -n "$zone" ]; then
        echo "power_long_uw=$(oc_read "$zone/constraint_0_power_limit_uw" || true)"
        echo "power_long_max_uw=$(oc_read "$zone/constraint_0_max_power_uw" || true)"
        echo "power_short_uw=$(oc_read "$zone/constraint_1_power_limit_uw" || true)"
        echo "power_short_max_uw=$(oc_read "$zone/constraint_1_max_power_uw" || true)"
    fi
    conf="$(oc_config_txt)"
    if [ -f "$conf" ]; then
        echo "config_txt=$conf"
        for key in arm_freq over_voltage sdram_freq gpu_freq; do
            last="$(sed -n "s/^$key=//p" "$conf" | tail -n1)"
            [ -n "$last" ] && echo "rpi_$key=$last"
        done
    fi
    echo "persisted=$([ -f "$CONF_FILE" ] && echo 1 || echo 0)"
    echo "boot_count=$(oc_boot_count_read)"
}
