# Sourced, not executed: no root check of its own; the caller owns that gate.

if ! command -v log >/dev/null 2>&1; then
    log() { printf '[vos-partition-lib] %s\n' "$*" >&2; }
fi

_PARTLIB_SETTLE_MS=0
_PARTLIB_LUKS_OPEN=""
_PARTLIB_MOUNT_OPEN=""

_partlib_kv_split() {
    case "$1" in
        *=*) ;;
        *) return 1 ;;
    esac
    _KV_KEY="${1%%=*}"
    _KV_VAL="${1#*=}"
    return 0
}

_partlib_foreach_record() {
    _prf_file="$1"
    _prf_cb="$2"
    _RECORD_KIND=""
    _RECORD_RAW=""
    _PARTLIB_ABORT=0

    while IFS= read -r _prf_line || [ -n "$_prf_line" ]; do
        case "$_prf_line" in
            ''|'#'*) continue ;;
        esac
        case "$_prf_line" in
            \[*\])
                if [ -n "$_RECORD_KIND" ]; then
                    "$_prf_cb"
                    [ "$_PARTLIB_ABORT" -ne 0 ] && return 0
                fi
                _RECORD_KIND="${_prf_line#\[}"
                _RECORD_KIND="${_RECORD_KIND%\]}"
                _RECORD_RAW=""
                continue
                ;;
        esac
        [ -n "$_RECORD_KIND" ] || continue
        _RECORD_RAW="${_RECORD_RAW}${_RECORD_RAW:+
}$_prf_line"
    done < "$_prf_file"

    [ -n "$_RECORD_KIND" ] && "$_prf_cb"
    return 0
}

_partlib_rec_get() {
    printf '%s\n' "$_RECORD_RAW" | sed -n "s|^$1=||p" | tail -n1
}

_partlib_rec_get_all() {
    printf '%s\n' "$_RECORD_RAW" | sed -n "s|^$1=||p"
}

_partlib_field_ok() {
    case "$1" in
        *"
"*) return 1 ;;
    esac
    return 0
}

# sfdisk silently writes type 0 for a GUID on a dos label; keep hex codes here.
_partlib_mbr_type_hex() {
    case "$1" in
        esp)        printf '%s\n' "ef" ;;
        linux)      printf '%s\n' "83" ;;
        linux_swap) printf '%s\n' "82" ;;
        bios_boot)  printf '%s\n' "83" ;;
        *)          printf '%s\n' "$1" ;;
    esac
}


_partlib_gpt_type_guid() {
    case "$1" in
        esp)        printf '%s\n' "C12A7328-F81F-11D2-BA4B-00A0C93EC93B" ;;
        linux)      printf '%s\n' "0FC63DAF-8483-4772-8E79-3D69D8477DE4" ;;
        linux_swap) printf '%s\n' "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F" ;;
        bios_boot)  printf '%s\n' "21686148-6449-6E6F-744F-656E63656453" ;;
        *)          printf '%s\n' "$1" ;;
    esac
}

_partlib_last_node() {
    sfdisk --dump "$1" 2>/dev/null | awk '/^\// { node=$1 } END { if (node != "") print node }'
}

_partlib_partno() {
    sfdisk --dump "$1" 2>/dev/null | awk -v want="$2" '
        /^\// { n++; if ($1 == want) { print n; f=1; exit } }
        END { exit !f }
    '
}

_partlib_table_type() {
    sfdisk --dump "$1" 2>/dev/null | awk -F': ' '/^label:/ { print $2; exit }'
}

_partlib_settle() {
    _disk="$1"
    [ -b "$_disk" ] || return 0
    _t0="$(date +%s%N)"
    udevadm settle --timeout=10 2>/dev/null || true
    _t1="$(date +%s%N)"
    _PARTLIB_SETTLE_MS=$(( _PARTLIB_SETTLE_MS + (_t1 - _t0) / 1000000 ))
    return 0
}

_partlib_wait_devnode() {
    _node="$1"
    _timeout="${2:-5}"
    _t0="$(date +%s%N)"
    _waited=0
    while [ ! -b "$_node" ]; do
        if [ "$_waited" -ge "$_timeout" ]; then
            _t1="$(date +%s%N)"
            _PARTLIB_SETTLE_MS=$(( _PARTLIB_SETTLE_MS + (_t1 - _t0) / 1000000 ))
            log "timed out waiting for $_node to appear"
            return 1
        fi
        sleep 1
        _waited=$((_waited + 1))
    done
    _t1="$(date +%s%N)"
    _PARTLIB_SETTLE_MS=$(( _PARTLIB_SETTLE_MS + (_t1 - _t0) / 1000000 ))
    return 0
}

_ID_MAP=""

_partlib_id_map_set() {
    _ID_MAP="$_ID_MAP
$1=$2"
}

_partlib_id_map_get() {
    printf '%s\n' "$_ID_MAP" | sed -n "s|^$1=||p" | tail -n1
}

_partlib_create_table() {
    _disk="$1"
    _type="$2"
    case "$_type" in
        gpt) _label=gpt ;;
        mbr) _label=dos ;;
        *) log "create_table: invalid type: $_type"; return 1 ;;
    esac
    _err="$(printf 'label: %s\n' "$_label" \
        | sfdisk --wipe=always --wipe-partitions=always "$_disk" 2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "create_table: sfdisk failed on $_disk: $_err"
        return 1
    fi
    _partlib_settle "$_disk"
    return 0
}

_partlib_create() {
    _disk="$1"
    _start="$2"
    _size="$3"
    _gtype="$4"
    if [ "$(_partlib_table_type "$_disk")" = "dos" ]; then
        _type="$(_partlib_mbr_type_hex "$_gtype")"
    else
        _type="$(_partlib_gpt_type_guid "$_gtype")"
    fi
    if [ "$_size" = "-1" ]; then
        _line="start=${_start}MiB, type=${_type}"
    else
        _line="start=${_start}MiB, size=${_size}MiB, type=${_type}"
    fi

    _err="$(printf '%s\n' "$_line" | sfdisk --append "$_disk" 2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "create: sfdisk --append failed on $_disk: $_err"
        return 1
    fi
    _partlib_settle "$_disk"

    _node="$(_partlib_last_node "$_disk")"
    if [ -z "$_node" ]; then
        log "create: could not determine resulting partition node on $_disk"
        return 1
    fi
    if [ -b "$_disk" ]; then
        _partlib_wait_devnode "$_node" || return 1
        # wipefs: old signatures survive --append and surface as ghost fs.
        if [ -b "$_node" ]; then
            _err="$(wipefs --all "$_node" 2>&1 >/dev/null)" \
                || log "create: wipefs failed on $_node (continuing): $_err"
            _partlib_settle "$_disk"
        fi
    fi
    printf '%s\n' "$_node"
    return 0
}

_partlib_delete() {
    _disk="$1"
    _target="$2"
    _partno="$(_partlib_partno "$_disk" "$_target")"
    if [ -z "$_partno" ]; then
        log "delete: could not find partition number for $_target on $_disk"
        return 1
    fi
    _err="$(sfdisk --delete "$_disk" "$_partno" 2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "delete: sfdisk --delete failed for $_target (partno=$_partno): $_err"
        return 1
    fi
    _partlib_settle "$_disk"
    return 0
}

_partlib_set_flags() {
    _disk="$1"
    _target="$2"
    _flags="$3"

    _partno="$(_partlib_partno "$_disk" "$_target")"
    if [ -z "$_partno" ]; then
        log "set_flags: could not find partition number for $_target on $_disk"
        return 1
    fi
    _table="$(_partlib_table_type "$_disk")"

    _bits=""
    for _f in $_flags; do
        case "$_table:$_f" in
            gpt:boot|gpt:legacy_boot)
                _bits="${_bits:+$_bits,}LegacyBIOSBootable" ;;
            gpt:no_automount)
                _bits="${_bits:+$_bits,}NoBlockIOProtocol" ;;
            gpt:read_only)
                _bits="${_bits:+$_bits,}RequiredPartition" ;;
            gpt:esp)
                if ! sfdisk --part-type "$_disk" "$_partno" \
                        C12A7328-F81F-11D2-BA4B-00A0C93EC93B >/dev/null 2>&1; then
                    log "set_flags: sfdisk --part-type (esp) failed on $_disk#$_partno"
                    return 1
                fi
                ;;
            dos:boot)
                if ! sfdisk --activate "$_disk" "$_partno" >/dev/null 2>&1; then
                    log "set_flags: sfdisk --activate failed on $_disk#$_partno"
                    return 1
                fi
                ;;
            *)
                log "set_flags: flag '$_f' not supported on table type '$_table'"
                return 1
                ;;
        esac
    done

    if [ -n "$_bits" ]; then
        if ! sfdisk --part-attrs "$_disk" "$_partno" "$_bits" >/dev/null 2>&1; then
            log "set_flags: sfdisk --part-attrs failed on $_disk#$_partno ($_bits)"
            return 1
        fi
    fi

    _partlib_settle "$_disk"
    return 0
}

_partlib_erase() {
    _devnode="$1"
    _err="$(wipefs -a "$_devnode" 2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "erase: wipefs failed for $_devnode: $_err"
        return 1
    fi
    return 0
}

_partlib_repair() {
    _devnode="$1"
    _fs="$2"
    _check_only="$3"

    case "$_fs" in
        ext4)
            if [ "$_check_only" -eq 1 ]; then
                _err="$(e2fsck -n -f "$_devnode" 2>&1 >/dev/null)"
            else
                _err="$(e2fsck -p -f "$_devnode" 2>&1 >/dev/null)"
            fi
            _rc=$?
            if [ "$_rc" -ge 4 ]; then
                log "repair: e2fsck reported problems on $_devnode (rc=$_rc): $_err"
                return 1
            fi
            return 0
            ;;
        fat32)
            if [ "$_check_only" -eq 1 ]; then
                _err="$(fsck.fat -n "$_devnode" 2>&1 >/dev/null)"
            else
                _err="$(fsck.fat -a "$_devnode" 2>&1 >/dev/null)"
            fi
            ;;
        xfs)
            if [ "$_check_only" -eq 1 ]; then
                _err="$(xfs_repair -n "$_devnode" 2>&1 >/dev/null)"
            else
                _err="$(xfs_repair "$_devnode" 2>&1 >/dev/null)"
            fi
            ;;
        btrfs)
            if [ "$_check_only" -eq 1 ]; then
                _err="$(btrfs check "$_devnode" 2>&1 >/dev/null)"
            else
                _err="$(btrfs check --repair "$_devnode" 2>&1 >/dev/null)"
            fi
            ;;
        *)
            log "repair: unsupported filesystem: $_fs"
            return 1
            ;;
    esac
    if [ $? -ne 0 ]; then
        log "repair: check/repair failed for $_devnode (fs=$_fs): $_err"
        return 1
    fi
    return 0
}

_partlib_set_label() {
    _devnode="$1"
    _fs="$2"
    _label="$3"

    case "$_fs" in
        ext4)  _err="$(e2label "$_devnode" "$_label" 2>&1 >/dev/null)" ;;
        fat32) _err="$(fatlabel "$_devnode" "$_label" 2>&1 >/dev/null)" ;;
        btrfs) _err="$(btrfs filesystem label "$_devnode" "$_label" 2>&1 >/dev/null)" ;;
        xfs)   _err="$(xfs_admin -L "$_label" "$_devnode" 2>&1 >/dev/null)" ;;
        swap)  _err="$(swaplabel -L "$_label" "$_devnode" 2>&1 >/dev/null)" ;;
        *)     log "set_label: unsupported filesystem: $_fs"; return 1 ;;
    esac
    if [ $? -ne 0 ]; then
        log "set_label: failed for $_devnode (fs=$_fs): $_err"
        return 1
    fi
    return 0
}

_partlib_set_gpt_name() {
    _disk="$1"
    _target="$2"
    _name="$3"

    _partno="$(_partlib_partno "$_disk" "$_target")"
    if [ -z "$_partno" ]; then
        log "set_gpt_name: could not find partition number for $_target on $_disk"
        return 1
    fi
    _err="$(sfdisk --part-label "$_disk" "$_partno" "$_name" 2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "set_gpt_name: sfdisk --part-label failed on $_disk#$_partno: $_err"
        return 1
    fi
    return 0
}

_partlib_part_size_mib() {
    _disk="$1"
    _target="$2"
    sfdisk --dump "$_disk" 2>/dev/null | awk -v want="$_target" '
        /^sector-size:/ { ss = $2 }
        $1 == want {
            for (i = 2; i <= NF; i++) {
                if ($i == "size=") {
                    val = $(i + 1); gsub(",", "", val)
                    print int(val * ss / 1048576); exit
                }
                if ($i ~ /^size=[0-9]/) {
                    val = $i; gsub("size=|,", "", val)
                    print int(val * ss / 1048576); exit
                }
            }
        }
    '
}

# Crash-safe partition move: sfdisk --move-data commits the new start before the copy finishes, so copy
# first under a durable journal in /var/lib/vos, switch the table after fsync, then clear the journal.

_PARTLIB_MOVE_JOURNAL_DIR="${VOS_MOVE_JOURNAL_DIR:-/var/lib/vos/move-journal}"
_PARTLIB_MOVE_CHUNK="${VOS_MOVE_CHUNK:-4194304}"

_partlib_fsync_path() {
    _fp="$1"
    python3 -c 'import os,sys; fd=os.open(sys.argv[1], os.O_RDONLY); os.fsync(fd); os.close(fd)' \
        "$_fp" 2>/dev/null || true
}

_partlib_move_journal_name() {
    _mj_disk="$1"
    _mj_partno="$2"
    _mj_key="$(printf '%s' "$_mj_disk" | sed 's|^/dev/||; s|/|-|g')"
    printf '%s-%s.journal\n' "$_mj_key" "$_mj_partno"
}

_partlib_move_journal_path() {
    printf '%s/%s\n' "$_PARTLIB_MOVE_JOURNAL_DIR" \
        "$(_partlib_move_journal_name "$1" "$2")"
}

# Globals _MJ_* hold the record being written. Atomic: tmp, fsync, rename.
_partlib_move_journal_write() {
    _mj_path="$1"
    _mj_dir="$(dirname "$_mj_path")"
    if ! mkdir -p "$_mj_dir" 2>/dev/null; then
        log "move: cannot create journal dir $_mj_dir"
        return 1
    fi
    _mj_tmp="$_mj_path.tmp.$$"
    {
        printf 'version=1\n'
        printf 'disk=%s\n' "$_MJ_DISK"
        printf 'partno=%s\n' "$_MJ_PARTNO"
        printf 'target=%s\n' "$_MJ_TARGET"
        printf 'old_start=%s\n' "$_MJ_OLD"
        printf 'new_start=%s\n' "$_MJ_NEW"
        printf 'size=%s\n' "$_MJ_SIZE"
        printf 'copied=%s\n' "$_MJ_COPIED"
        printf 'state=%s\n' "$_MJ_STATE"
    } > "$_mj_tmp" || return 1
    _partlib_fsync_path "$_mj_tmp"
    mv -f "$_mj_tmp" "$_mj_path" || return 1
    _partlib_fsync_path "$_mj_dir"
    return 0
}

# Loads MJ_* from a journal file.
_partlib_move_journal_read() {
    _mj_path="$1"
    [ -f "$_mj_path" ] || return 1
    MJ_VERSION=""; MJ_DISK=""; MJ_PARTNO=""; MJ_TARGET=""
    MJ_OLD=""; MJ_NEW=""; MJ_SIZE=""; MJ_COPIED=""; MJ_STATE=""
    while IFS= read -r _mj_line || [ -n "$_mj_line" ]; do
        case "$_mj_line" in
            ''|'#'*) continue ;;
        esac
        _partlib_kv_split "$_mj_line" || continue
        case "$_KV_KEY" in
            version)   MJ_VERSION="$_KV_VAL" ;;
            disk)      MJ_DISK="$_KV_VAL" ;;
            partno)    MJ_PARTNO="$_KV_VAL" ;;
            target)    MJ_TARGET="$_KV_VAL" ;;
            old_start) MJ_OLD="$_KV_VAL" ;;
            new_start) MJ_NEW="$_KV_VAL" ;;
            size)      MJ_SIZE="$_KV_VAL" ;;
            copied)    MJ_COPIED="$_KV_VAL" ;;
            state)     MJ_STATE="$_KV_VAL" ;;
        esac
    done < "$_mj_path"
    [ -n "$MJ_DISK" ] && [ -n "$MJ_SIZE" ] || return 1
    return 0
}

_partlib_move_journal_clear() {
    _mj_path="$1"
    [ -f "$_mj_path" ] || return 0
    rm -f "$_mj_path"
    _partlib_fsync_path "$(dirname "$_mj_path")"
    return 0
}

# Prints "start_bytes size_bytes" for a partition node on disk.
_partlib_part_start_size() {
    _ps_disk="$1"
    _ps_target="$2"
    command -v sfdisk >/dev/null 2>&1 || return 1
    sfdisk --dump "$_ps_disk" 2>/dev/null | awk -v want="$_ps_target" '
        /^sector-size:/ { ss = $2 }
        $1 == want {
            st = ""; sz = ""
            for (i = 2; i <= NF; i++) {
                if ($i == "size=") {
                    sz = $(i + 1); gsub(",", "", sz)
                } else if ($i ~ /^size=[0-9]/) {
                    sz = $i; gsub("size=|,", "", sz)
                }
                if ($i == "start=") {
                    st = $(i + 1); gsub(",", "", st)
                } else if ($i ~ /^start=[0-9]/) {
                    st = $i; gsub("start=|,", "", st)
                }
            }
            if (st != "" && sz != "")
                printf "%d %d\n", st * ss, sz * ss
            exit
        }
    '
}

# Retarget only the table entry. Never copies; callers must have the
# data already at the new start and fsync'd.
_partlib_move_table_switch() {
    _mt_disk="$1"
    _mt_partno="$2"
    _mt_new_start_mib="$3"
    command -v sfdisk >/dev/null 2>&1 || {
        log "move: sfdisk not available"
        return 1
    }
    _err="$(printf 'start=%sMiB\n' "$_mt_new_start_mib" \
        | sfdisk --no-reread --force "$_mt_disk" -N "$_mt_partno" \
            2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "move: sfdisk table switch failed on $_mt_disk#$_mt_partno: $_err"
        return 1
    fi
    _partlib_settle "$_mt_disk"
    return 0
}

# Copy len bytes from src_off to dst_off on the same disk. forward=0 walks high to low, so
# overlapping ranges never overwrite source bytes that are still unread.
_partlib_move_copy_chunk() {
    _dc_disk="$1"
    _dc_src="$2"
    _dc_dst="$3"
    _dc_len="$4"
    _dc_forward="$5"
    _dc_journal="$6"

    _dc_pos=0
    while [ "$_dc_pos" -lt "$_dc_len" ]; do
        _dc_n="$_PARTLIB_MOVE_CHUNK"
        _dc_rem=$(( _dc_len - _dc_pos ))
        if [ "$_dc_rem" -lt "$_dc_n" ]; then
            _dc_n=$_dc_rem
        fi
        if [ "$_dc_forward" -eq 1 ]; then
            _dc_so=$(( _dc_src + _dc_pos ))
            _dc_do=$(( _dc_dst + _dc_pos ))
        else
            _dc_so=$(( _dc_src + _dc_len - _dc_pos - _dc_n ))
            _dc_do=$(( _dc_dst + _dc_len - _dc_pos - _dc_n ))
        fi
        if ! dd if="$_dc_disk" of="$_dc_disk" bs="$_PARTLIB_MOVE_CHUNK" \
            iflag=fullblock,count_bytes,skip_bytes oflag=seek_bytes \
            skip="$_dc_so" seek="$_dc_do" count="$_dc_n" \
            conv=notrunc,fsync 2>/dev/null; then
            log "move: copy failed at offset $_dc_so"
            return 1
        fi
        _dc_pos=$(( _dc_pos + _dc_n ))
        if [ -n "$_dc_journal" ] && [ -f "$_dc_journal" ]; then
            if _partlib_move_journal_read "$_dc_journal"; then
                MJ_COPIED=$_dc_pos
                _partlib_move_journal_write "$_dc_journal" || true
            fi
        fi
    done
    return 0
}

# Resume a journaled move to completion: finish the copy, switch table,
# clear the journal.
_partlib_move_finish_from_journal() {
    _mf_path="$1"
    _partlib_move_journal_read "$_mf_path" || {
        log "move: cannot read journal $_mf_path"
        return 1
    }
    if [ -z "$MJ_TARGET" ] || { [ ! -e "$MJ_TARGET" ] && [ ! -b "$MJ_TARGET" ]; }
    then
        log "move: journal target missing: ${MJ_TARGET:-<empty>}"
        return 1
    fi
    if [ -z "$MJ_OLD" ] || [ -z "$MJ_NEW" ] || [ -z "$MJ_SIZE" ]; then
        log "move: journal incomplete: $_mf_path"
        return 1
    fi
    [ -n "$MJ_COPIED" ] || MJ_COPIED=0
    if [ "$MJ_COPIED" -lt 0 ] 2>/dev/null; then
        MJ_COPIED=0
    fi
    if [ "$MJ_COPIED" -gt "$MJ_SIZE" ] 2>/dev/null; then
        MJ_COPIED="$MJ_SIZE"
    fi

    _mf_old_mib=$(( MJ_OLD / 1048576 ))
    _mf_new_mib=$(( MJ_NEW / 1048576 ))
    _mf_remain=$(( MJ_SIZE - MJ_COPIED ))

    if [ "$_mf_remain" -gt 0 ]; then
        if [ "$MJ_NEW" -gt "$MJ_OLD" ]; then
            _mf_forward=0
        else
            _mf_forward=1
        fi
        _mf_src=$(( MJ_OLD + MJ_COPIED ))
        _mf_dst=$(( MJ_NEW + MJ_COPIED ))
        log "move: resuming copy for $MJ_TARGET ($_mf_remain bytes left)"
        if ! _partlib_move_copy_chunk "$MJ_DISK" "$_mf_src" "$_mf_dst" \
            "$_mf_remain" "$_mf_forward" "$_mf_path"; then
            return 1
        fi
    fi

    _partlib_fsync_path "$MJ_DISK"
    MJ_STATE=table_pending
    MJ_COPIED="$MJ_SIZE"
    _partlib_move_journal_write "$_mf_path" || true
    if ! _partlib_move_table_switch "$MJ_DISK" "$MJ_PARTNO" "$_mf_new_mib"
    then
        log "move: table switch failed after copy for $MJ_TARGET"
        return 1
    fi
    _partlib_move_journal_clear "$_mf_path"
    log "move: recovered $MJ_TARGET ($_mf_old_mib MiB -> $_mf_new_mib MiB)"
    return 0
}

# Undo a journaled move: put the already-copied prefix back at the old
# start, then clear the journal. The table stays at the old start.
_partlib_move_rollback_journal() {
    _mr_path="$1"
    _partlib_move_journal_read "$_mr_path" || {
        log "move: cannot read journal $_mr_path"
        return 1
    }
    [ -n "$MJ_COPIED" ] || MJ_COPIED=0
    if [ "$MJ_COPIED" -le 0 ] 2>/dev/null; then
        _partlib_move_journal_clear "$_mr_path"
        log "move: rolled back $MJ_TARGET (nothing copied)"
        return 0
    fi
    if [ "$MJ_COPIED" -gt "$MJ_SIZE" ] 2>/dev/null; then
        MJ_COPIED="$MJ_SIZE"
    fi
    if [ "$MJ_NEW" -gt "$MJ_OLD" ]; then
        _mr_forward=1
    else
        _mr_forward=0
    fi
    if ! _partlib_move_copy_chunk "$MJ_DISK" "$MJ_NEW" "$MJ_OLD" \
        "$MJ_COPIED" "$_mr_forward" ""; then
        log "move: rollback copy failed for $MJ_TARGET"
        return 1
    fi
    _partlib_fsync_path "$MJ_DISK"
    _partlib_move_journal_clear "$_mr_path"
    log "move: rolled back $MJ_TARGET to start $MJ_OLD"
    return 0
}

# action=status|resume|rollback. Prints key=value lines on stdout.
_partlib_move_recover_disk() {
    _rd_disk="$1"
    _rd_action="${2:-resume}"
    _rd_dir="$_PARTLIB_MOVE_JOURNAL_DIR"
    _rd_found=0
    _rd_overall="none"
    _rd_detail=""

    if [ ! -d "$_rd_dir" ]; then
        printf 'move_recovery=none\n'
        printf 'move_recovery_detail=no move journal directory\n'
        return 0
    fi

    for _rd_path in "$_rd_dir"/*.journal; do
        [ -f "$_rd_path" ] || continue
        _partlib_move_journal_read "$_rd_path" || continue
        [ "$MJ_DISK" = "$_rd_disk" ] || continue
        _rd_found=1
        _rd_target="${MJ_TARGET:-unknown}"
        printf 'journal=%s\n' "$_rd_path"
        printf 'j_disk=%s\n' "$MJ_DISK"
        printf 'j_partno=%s\n' "$MJ_PARTNO"
        printf 'j_target=%s\n' "$_rd_target"
        printf 'j_copied=%s\n' "${MJ_COPIED:-0}"
        printf 'j_size=%s\n' "$MJ_SIZE"
        printf 'j_state=%s\n' "${MJ_STATE:-unknown}"
        printf 'j_old_start=%s\n' "${MJ_OLD:-}"
        printf 'j_new_start=%s\n' "${MJ_NEW:-}"

        case "$_rd_action" in
            status)
                _rd_overall="pending"
                _rd_detail="${_rd_detail:+$_rd_detail; }$_rd_target state=${MJ_STATE:-unknown} copied=${MJ_COPIED:-0}/${MJ_SIZE}"
                ;;
            resume)
                if _partlib_move_finish_from_journal "$_rd_path"; then
                    _rd_overall="resumed"
                    _rd_detail="${_rd_detail:+$_rd_detail; }resumed $_rd_target"
                else
                    _rd_overall="failed"
                    _rd_detail="${_rd_detail:+$_rd_detail; }resume failed for $_rd_target"
                fi
                ;;
            rollback)
                if _partlib_move_rollback_journal "$_rd_path"; then
                    _rd_overall="rolled_back"
                    _rd_detail="${_rd_detail:+$_rd_detail; }rolled back $_rd_target"
                else
                    _rd_overall="failed"
                    _rd_detail="${_rd_detail:+$_rd_detail; }rollback failed for $_rd_target"
                fi
                ;;
            *)
                log "move-recovery: unknown action $_rd_action"
                _rd_overall="failed"
                ;;
        esac
    done

    if [ "$_rd_found" -eq 0 ]; then
        printf 'move_recovery=none\n'
        printf 'move_recovery_detail=no pending move journals for %s\n' "$_rd_disk"
        return 0
    fi

    printf 'move_recovery=%s\n' "$_rd_overall"
    printf 'move_recovery_detail=%s\n' "$_rd_detail"
    return 0
}

_partlib_move() {
    _disk="$1"
    _target="$2"
    _new_start_mib="$3"

    _partno="$(_partlib_partno "$_disk" "$_target")"
    if [ -z "$_partno" ]; then
        log "move: could not find partition number for $_target on $_disk"
        return 1
    fi

    _ss="$(_partlib_part_start_size "$_disk" "$_target")" || {
        log "move: could not read start/size for $_target on $_disk"
        return 1
    }
    _old_start="$(printf '%s\n' "$_ss" | awk '{print $1}')"
    _size="$(printf '%s\n' "$_ss" | awk '{print $2}')"
    if [ -z "$_old_start" ] || [ -z "$_size" ] || [ "$_size" -le 0 ]; then
        log "move: invalid geometry for $_target on $_disk"
        return 1
    fi

    _new_start=$(( _new_start_mib * 1048576 ))
    if [ "$_new_start" -eq "$_old_start" ]; then
        log "move: $_target already starts at $_new_start_mib MiB"
        return 0
    fi

    _disk_bytes="$(blockdev --getsize64 "$_disk" 2>/dev/null || echo 0)"
    if [ -n "$_disk_bytes" ] && [ "$_disk_bytes" -gt 0 ] 2>/dev/null; then
        if [ $(( _new_start + _size )) -gt "$_disk_bytes" ]; then
            log "move: new start $_new_start_mib MiB plus size exceeds disk"
            return 1
        fi
    fi

    _MJ_DISK="$_disk"
    _MJ_PARTNO="$_partno"
    _MJ_TARGET="$_target"
    _MJ_OLD="$_old_start"
    _MJ_NEW="$_new_start"
    _MJ_SIZE="$_size"
    _MJ_COPIED=0
    _MJ_STATE=copying
    _journal="$(_partlib_move_journal_path "$_disk" "$_partno")"
    if ! _partlib_move_journal_write "$_journal"; then
        log "move: cannot write move journal $_journal"
        return 1
    fi

    if [ "$_new_start" -gt "$_old_start" ]; then
        _forward=0
    else
        _forward=1
    fi
    if ! _partlib_move_copy_chunk "$_disk" "$_old_start" "$_new_start" \
        "$_size" "$_forward" "$_journal"; then
        log "move: data copy failed for $_target; journal left at $_journal"
        return 1
    fi

    _partlib_fsync_path "$_disk"
    _MJ_COPIED="$_size"
    _MJ_STATE=table_pending
    _partlib_move_journal_write "$_journal" || true

    if ! _partlib_move_table_switch "$_disk" "$_partno" "$_new_start_mib"
    then
        log "move: table switch failed for $_target; journal left at $_journal"
        return 1
    fi
    _partlib_move_journal_clear "$_journal"
    log "move: $_target moved to $_new_start_mib MiB"
    return 0
}

_partlib_table_resize() {
    _disk="$1"
    _partno="$2"
    _size_mib="$3"
    _err="$(printf 'size=%sMiB\n' "$_size_mib" \
        | sfdisk --no-reread --force "$_disk" -N "$_partno" 2>&1 >/dev/null)"
    if [ $? -ne 0 ]; then
        log "resize: sfdisk table resize failed on $_disk#$_partno: $_err"
        return 1
    fi
    _partlib_settle "$_disk"
    return 0
}

_partlib_mount_track() {
    _PARTLIB_MOUNT_OPEN="$_PARTLIB_MOUNT_OPEN
$1"
}

_partlib_mount_untrack() {
    _PARTLIB_MOUNT_OPEN="$(printf '%s\n' "$_PARTLIB_MOUNT_OPEN" | grep -Fxv "$1" || true)"
}

_partlib_umount_all() {
    _m_saved="$_PARTLIB_MOUNT_OPEN"
    _PARTLIB_MOUNT_OPEN=""
    printf '%s\n' "$_m_saved" | while IFS= read -r _m; do
        [ -n "$_m" ] || continue
        _partlib_safe_umount "$_m"
        rmdir "$_m" 2>/dev/null || true
    done
}

# umount can transiently fail busy after fs ops; retry, then lazy umount.
_partlib_safe_umount() {
    _su_mnt="$1"
    _su_try=0
    while [ "$_su_try" -lt 5 ]; do
        umount "$_su_mnt" 2>/dev/null && return 0
        _su_try=$((_su_try + 1))
        sleep 0.2
    done
    umount -l "$_su_mnt" 2>/dev/null
}

# btrfs has no offline resize; mount at a private tracked point to resize.
_partlib_btrfs_resize_mounted() {
    _target="$1"
    _size_mib="$2"

    mkdir -p /run/vos-partition-helper 2>/dev/null || true
    _mnt="$(mktemp -d /run/vos-partition-helper/mnt-XXXXXX 2>/dev/null)"
    if [ -z "$_mnt" ]; then
        log "resize: could not create a temporary mountpoint for btrfs resize"
        return 1
    fi

    if ! mount "$_target" "$_mnt" 2>/dev/null; then
        log "resize: could not mount $_target for btrfs resize"
        rmdir "$_mnt" 2>/dev/null || true
        return 1
    fi
    _partlib_mount_track "$_mnt"

    if [ -n "$_size_mib" ]; then
        _err="$(btrfs filesystem resize "${_size_mib}M" "$_mnt" 2>&1 >/dev/null)"
    else
        _err="$(btrfs filesystem resize max "$_mnt" 2>&1 >/dev/null)"
    fi
    _rc=$?

    _partlib_safe_umount "$_mnt"
    _partlib_mount_untrack "$_mnt"
    rmdir "$_mnt" 2>/dev/null || true

    if [ "$_rc" -ne 0 ]; then
        log "resize: btrfs filesystem resize failed: $_err"
        return 1
    fi
    return 0
}

# Only exit 8 is retried: transient busy after unmount; 4+ is a real finding.
_partlib_e2fsck_check() {
    _ec_target="$1"
    _ec_try=0
    while [ "$_ec_try" -lt 5 ]; do
        _ec_err="$(e2fsck -f -y "$_ec_target" 2>&1 >/dev/null)"
        _ec_rc=$?
        [ "$_ec_rc" -ne 8 ] && break
        _ec_try=$((_ec_try + 1))
        sleep 0.2
    done
    printf '%s' "$_ec_err"
    return "$_ec_rc"
}

_partlib_resize() {
    _disk="$1"
    _target="$2"
    _fs="$3"
    _new_size_mib="$4"

    if [ "$_fs" != "btrfs" ]; then
        if _mnt="$(findmnt -n -o TARGET --source "$_target" 2>/dev/null)" \
                && [ -n "$_mnt" ]; then
            log "resize: $_target is mounted at $_mnt; unmount it first"
            return 1
        fi
    fi

    _partno="$(_partlib_partno "$_disk" "$_target")"
    if [ -z "$_partno" ]; then
        log "resize: could not find partition number for $_target on $_disk"
        return 1
    fi
    _cur_size_mib="$(_partlib_part_size_mib "$_disk" "$_target")"
    if [ -z "$_cur_size_mib" ] || [ "$_cur_size_mib" -le 0 ]; then
        log "resize: could not read current size of $_target"
        return 1
    fi

    _grow=1
    [ "$_new_size_mib" -lt "$_cur_size_mib" ] && _grow=0

    case "$_fs:$_grow" in
        ext4:0)
            _err="$(_partlib_e2fsck_check "$_target")"
            if [ $? -ge 4 ]; then
                log "resize: e2fsck pre-shrink failed on $_target: $_err"
                return 1
            fi
            _err="$(resize2fs "$_target" "${_new_size_mib}M" 2>&1 >/dev/null)"
            if [ $? -ne 0 ]; then
                log "resize: resize2fs shrink failed on $_target: $_err"
                return 1
            fi
            _partlib_table_resize "$_disk" "$_partno" "$_new_size_mib" || return 1
            ;;
        ext4:1)
            _partlib_table_resize "$_disk" "$_partno" "$_new_size_mib" || return 1
            _err="$(_partlib_e2fsck_check "$_target")"
            if [ $? -ge 4 ]; then
                log "resize: e2fsck pre-grow failed on $_target: $_err"
                return 1
            fi
            _err="$(resize2fs "$_target" 2>&1 >/dev/null)"
            if [ $? -ne 0 ]; then
                log "resize: resize2fs grow failed on $_target: $_err"
                return 1
            fi
            ;;
        xfs:1)
            _partlib_table_resize "$_disk" "$_partno" "$_new_size_mib" || return 1
            _err="$(xfs_growfs "$_target" 2>&1 >/dev/null)"
            if [ $? -ne 0 ]; then
                log "resize: xfs_growfs failed on $_target: $_err"
                return 1
            fi
            ;;
        btrfs:1)
            _partlib_table_resize "$_disk" "$_partno" "$_new_size_mib" || return 1
            _partlib_btrfs_resize_mounted "$_target" "" || return 1
            ;;
        btrfs:0)
            _partlib_btrfs_resize_mounted "$_target" "$_new_size_mib" || return 1
            _partlib_table_resize "$_disk" "$_partno" "$_new_size_mib" || return 1
            ;;
        swap:*)
            _partlib_table_resize "$_disk" "$_partno" "$_new_size_mib" || return 1
            _err="$(mkswap "$_target" 2>&1 >/dev/null)"
            if [ $? -ne 0 ]; then
                log "resize: mkswap failed on $_target after table resize: $_err"
                return 1
            fi
            ;;
        *)
            log "resize: unsupported for filesystem=$_fs grow=$_grow" \
                "(shrink unsupported for xfs, resize unsupported for" \
                "fat32 at all: P3-2c, permanent decision)"
            return 1
            ;;
    esac
    return 0
}

# Mandatory GRUB-safety excludes always apply; callers may only add more.
_partlib_ext4_exclude_opt() {
    _extra="$1"
    _all="orphan_file metadata_csum_seed casefold encrypt verity"
    for _f in $_extra; do
        case " $_all " in
            *" $_f "*) ;;
            *) _all="$_all $_f" ;;
        esac
    done
    _str=""
    for _f in $_all; do
        _str="${_str}^${_f},"
    done
    printf '%s\n' "${_str%,}"
}

_partlib_format() {
    _devnode="$1"
    _fs="$2"
    _label="$3"
    _ext4_extra="$4"

    case "$_fs" in
        ext4)
            _optstr="$(_partlib_ext4_exclude_opt "$_ext4_extra")"
            if [ -n "$_label" ]; then
                mkfs.ext4 -q -F -O "$_optstr" -L "$_label" "$_devnode" >/dev/null 2>&1
            else
                mkfs.ext4 -q -F -O "$_optstr" "$_devnode" >/dev/null 2>&1
            fi
            ;;
        fat32)
            if [ -n "$_label" ]; then
                mkfs.fat -F 32 -n "$_label" "$_devnode" >/dev/null 2>&1
            else
                mkfs.fat -F 32 "$_devnode" >/dev/null 2>&1
            fi
            ;;
        btrfs)
            if [ -n "$_label" ]; then
                mkfs.btrfs -f -q -L "$_label" "$_devnode" >/dev/null 2>&1
            else
                mkfs.btrfs -f -q "$_devnode" >/dev/null 2>&1
            fi
            ;;
        xfs)
            if [ -n "$_label" ]; then
                mkfs.xfs -f -L "$_label" "$_devnode" >/dev/null 2>&1
            else
                mkfs.xfs -f "$_devnode" >/dev/null 2>&1
            fi
            ;;
        swap)
            if [ -n "$_label" ]; then
                mkswap -L "$_label" "$_devnode" >/dev/null 2>&1
            else
                mkswap "$_devnode" >/dev/null 2>&1
            fi
            ;;
        *)
            log "format: unsupported filesystem: $_fs"
            return 1
            ;;
    esac
    if [ $? -ne 0 ]; then
        log "format: mkfs failed for $_devnode (fs=$_fs)"
        return 1
    fi

    _uuid="$(blkid -p -s UUID -o value "$_devnode" 2>/dev/null)"
    printf '%s\n' "$_uuid"
    return 0
}

_partlib_luks_track() {
    _PARTLIB_LUKS_OPEN="$_PARTLIB_LUKS_OPEN $1"
}

_partlib_luks_close_all() {
    for _m in $_PARTLIB_LUKS_OPEN; do
        cryptsetup luksClose "$_m" 2>/dev/null || true
    done
    _PARTLIB_LUKS_OPEN=""
}
