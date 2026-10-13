#!/bin/sh
# Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
# All rights reserved. Distributed under the terms of the MIT License.
#
# Host simulation of the Installer backend against sparse file disks:
# plan apply, format, configure, bootloader, invariants, failures.
# Needs no mounts or loop devices; where a step needs those (real
# devnode settle, grub-install on a block device, btrfs subvolume
# creation) it logs exactly that and verifies the rest.

set -u

. "$(dirname -- "$0")/harness-lib.sh"

WORK="$HARNESS_DIR/work"
rm -rf "$WORK"
mkdir -p "$WORK"

PASS=0
FAIL=0

ok() {
	PASS=$((PASS + 1))
	printf 'ok   %s\n' "$1"
}

bad() {
	FAIL=$((FAIL + 1))
	printf 'FAIL %s\n' "$1" >&2
}

check() {
	# check <description> <command...>
	_desc="$1"; shift
	if "$@" >/dev/null 2>&1; then
		ok "$_desc"
	else
		bad "$_desc"
	fi
}

check_not() {
	_desc="$1"; shift
	if "$@" >/dev/null 2>&1; then
		bad "$_desc"
	else
		ok "$_desc"
	fi
}

# sfdisk --json partition list as "start sectors:size sectors:type" lines.
table_lines() {
	sfdisk --json "$1" 2>/dev/null | python3 -c '
import json, sys
data = json.load(sys.stdin)
for p in data.get("partitiontable", {}).get("partitions", []):
    print("%s:%s:%s" % (p["start"], p["size"], p.get("type", "")))
'
}

harness_extract "$WORK/helper-functions-test.sh"
harness_write_apply_driver "$WORK/drive-apply.sh"
HELPER="$HARNESS_SRC_ROOT/data/libexec/vos-install-helper"

# ---------------------------------------------------------------- staged root
make_staged_root() {
	_root="$1"
	rm -rf "$_root"
	mkdir -p "$_root/boot/grub" "$_root/etc/vos" "$_root/home" \
		"$_root/usr/share/vos/grub" "$_root/var/lib/vos" \
		"$_root/usr/share/vos/installer" "$_root/etc/ssh"
	printf 'kernel-stub\n' > "$_root/boot/vmlinuz-6.12.0-rt"
	printf 'initrd-stub\n' > "$_root/boot/initrd.img-6.12.0-rt"
	printf 'GRUB_TIMEOUT=5\n' > "$_root/usr/share/vos/grub/grub"
	cp "$HARNESS_SRC_ROOT/data/etc/installer/excludes.list" \
		"$_root/usr/share/vos/installer/excludes.list"
}

# Foreign EFI content that must survive a V\OS bootloader install.
make_foreign_esp() {
	_dir="$1"; _img="$2"
	rm -rf "$_dir"; rm -f "$_img"
	mkdir -p "$_dir/EFI/BOOT" "$_dir/EFI/OTHER"
	printf 'foreign-fallback\n' > "$_dir/EFI/BOOT/BOOTX64.EFI"
	printf 'foreign-loader\n' > "$_dir/EFI/OTHER/other.efi"
	truncate -s 64M "$_img"
	mkfs.fat -F 32 -n ESP "$_img" >/dev/null 2>&1
	mmd -i "$_img" ::/EFI ::/EFI/BOOT ::/EFI/OTHER
	mcopy -o -i "$_img" "$_dir/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
	mcopy -o -i "$_img" "$_dir/EFI/OTHER/other.efi" ::/EFI/OTHER/other.efi
}

esp_dir_digest() {
	( cd "$1" && find EFI -type f | sort | while read -r f; do
		printf '%s ' "$f"; md5sum <"$f" | awk '{print $1}'
	  done ) | md5sum | awk '{print $1}'
}

# grub-bios-setup cannot open regular files in this container; apply its
# documented layout (boot.img at LBA0 keeping the table, core.img at LBA1)
# with dd, after GRUB's own tools produced both images.
write_bios_payload() {
	_img="$1"; _grubdir="$2"
	cp /usr/lib/grub/i386-pc/boot.img "$_grubdir/boot.img"
	grub-mkimage -O i386-pc -p '(hd0)/boot/grub' -o "$_grubdir/core.img" \
		part_msdos part_gpt ext2 fat btrfs xfs linux normal chain boot \
		configfile search search_fs_uuid || return 1
	dd if="$_grubdir/boot.img" of="$_img" bs=1 count=440 conv=notrunc \
		2>/dev/null || return 1
	printf '\001\000\000\000' | dd of="$_img" bs=1 seek=92 conv=notrunc \
		2>/dev/null || return 1
	dd if="$_grubdir/core.img" of="$_img" bs=512 seek=1 conv=notrunc \
		2>/dev/null || return 1
	rm -f "$_grubdir/boot.img"
	return 0
}

# ------------------------------------------------- scenario A: blank disk
run_scenario_a() {
	_fs="$1"
	_d="$WORK/a-$_fs"
	mkdir -p "$_d"
	disk="$_d/disk.img"
	truncate -s 2048M "$disk"

	cat > "$_d/plan-table.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=create_table
id=t0
type=gpt

[op]
kind=create
id=p_esp
start_mib=1
size_mib=512
gpt_type=esp
role=esp

[op]
kind=create
id=p_root
start_mib=513
size_mib=-1
gpt_type=linux
role=root
EOF

	if "$WORK/drive-apply.sh" "$_d/plan-table.txt" >"$_d/apply1.log" 2>&1; then
		ok "A/$_fs plan apply create_table+creates"
	else
		bad "A/$_fs plan apply create_table+creates"
		return
	fi
	grep -q '^status=ok' "$_d/plan-table.txt.result" \
		&& ok "A/$_fs result status ok" \
		|| bad "A/$_fs result status ok"

	_table_before="$(harness_table_digest "$disk")"
	_esp_line="$(table_lines "$disk" | sed -n 1p)"
	_root_line="$(table_lines "$disk" | sed -n 2p)"
	_esp_start="${_esp_line%%:*}"; _rest="${_esp_line#*:}"
	_esp_size="${_rest%%:*}"
	_root_start="${_root_line%%:*}"; _rest="${_root_line#*:}"
	_root_size="${_rest%%:*}"
	[ "$_esp_start" = "2048" ] && [ "$_root_start" = "1050624" ] \
		&& ok "A/$_fs partition offsets (1MiB / 513MiB)" \
		|| bad "A/$_fs partition offsets (got $_esp_start/$_root_start)"

	truncate -s $((_esp_size * 512)) "$_d/esp.img"
	truncate -s $((_root_size * 512)) "$_d/root.img"

	# Format the stand-in files through the same helper path; real devnodes
	# would need loop devices.
	cat > "$_d/plan-format.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=format
id=f_esp
target_ref=$_d/esp.img
filesystem=fat32
label=ESP
role=esp

[op]
kind=format
id=f_root
target_ref=$_d/root.img
filesystem=$_fs
label=vitruvian-root
role=root
EOF

	# role=esp against a literal (pre-existing) ref must be refused
	# unless --allow-reformat-esp is passed: never clobber a shared ESP.
	check_not "A/$_fs ESP reformat guard refuses" \
		"$WORK/drive-apply.sh" "$_d/plan-format.txt"
	grep -q 'allow-reformat-esp' "$_d/plan-format.txt.result" \
		&& ok "A/$_fs ESP guard refusal is reported" \
		|| bad "A/$_fs ESP guard refusal is reported"

	if "$WORK/drive-apply.sh" "$_d/plan-format.txt" --allow-reformat-esp \
			>"$_d/apply2.log" 2>&1; then
		ok "A/$_fs format apply (esp + $_fs root)"
	else
		bad "A/$_fs format apply (esp + $_fs root)"
		return
	fi
	_root_uuid="$(sed -n '/^\[volume\]$/,/^\[op\]/ { s/^uuid=//p; }' \
		"$_d/plan-format.txt.result" | sed -n 2p)"
	[ -n "$_root_uuid" ] && ok "A/$_fs root volume uuid reported" \
		|| bad "A/$_fs root volume uuid reported"

	case "$_fs" in
	ext4)
		_feats="$(dumpe2fs -h "$_d/root.img" 2>/dev/null \
			| sed -n 's/^Filesystem features: *//p')"
		_missing=""
		for f in orphan_file metadata_csum_seed casefold encrypt verity; do
			printf ' %s ' "$_feats" | grep -q " $f " && _missing="$_missing $f"
		done
		[ -z "$_missing" ] && ok "A/ext4 mandatory feature exclusions" \
			|| bad "A/ext4 mandatory feature exclusions ($_missing)"
		;;
	xfs)
		xfs_db -c info "$_d/root.img" >/dev/null 2>&1 \
			&& ok "A/xfs xfs_db accepts the filesystem" \
			|| bad "A/xfs xfs_db accepts the filesystem"
		;;
	btrfs)
		btrfs check "$_d/root.img" >/dev/null 2>&1 \
			&& ok "A/btrfs btrfs check accepts the filesystem" \
			|| bad "A/btrfs btrfs check accepts the filesystem"
		;;
	esac

	# A real disk image: put the filesystems at their partition offsets.
	dd if="$_d/esp.img" of="$disk" bs=512 seek="$_esp_start" \
		conv=notrunc 2>/dev/null
	dd if="$_d/root.img" of="$disk" bs=512 seek="$_root_start" \
		conv=notrunc 2>/dev/null
	[ "$(harness_table_digest "$disk")" = "$_table_before" ] \
		&& ok "A/$_fs data injection leaves the table intact" \
		|| bad "A/$_fs data injection leaves the table intact"

	# Configure the staged target through the real helper entry point.
	make_staged_root "$_d/target"
	_VOS_CFG_ESP="$_d/esp.img" VOS_INSTALL_NO_SWAPFILE=1 \
		sh "$HELPER" configure "$_d/target" "$_d/root.img" "$_fs" \
		>"$_d/configure.log" 2>&1
	_rc=$?
	[ $_rc -eq 0 ] && ok "A/$_fs helper configure stage" \
		|| bad "A/$_fs helper configure stage (rc=$_rc)"

	_uuid_actual="$(blkid -p -s UUID -o value "$_d/root.img")"
	grep -q "^UUID=$_uuid_actual  /  $_fs" "$_d/target/etc/fstab" \
		&& ok "A/$_fs fstab root line (UUID + fs)" \
		|| bad "A/$_fs fstab root line (UUID + fs)"
	grep -q "/boot/efi" "$_d/target/etc/fstab" \
		&& ok "A/$_fs fstab ESP line from chosen ESP" \
		|| bad "A/$_fs fstab ESP line from chosen ESP"
	[ "$(cat "$_d/target/etc/vos/root-fstype")" = "$_fs" ] \
		&& ok "A/$_fs root-fstype recorded" \
		|| bad "A/$_fs root-fstype recorded"

	# btrfs: fstab and the GRUB cmdline must name @. Subvolume creation
	# needs a mount and is not covered here.
	if [ "$_fs" = "btrfs" ]; then
		grep -q 'subvol=@' "$_d/target/etc/fstab" \
			&& bad "A/btrfs flat fstab without subvolume state" \
			|| ok "A/btrfs flat fstab without subvolume state"
		mkdir -p "$_d/target/etc/vos"
		: > "$_d/target/etc/vos/btrfs-subvols"
		VOS_INSTALL_NO_SWAPFILE=1 \
			sh "$HELPER" configure "$_d/target" "$_d/root.img" btrfs \
			>>"$_d/configure.log" 2>&1
		grep -q 'defaults,subvol=@' "$_d/target/etc/fstab" \
			&& ok "A/btrfs fstab names @ when subvolumes exist" \
			|| bad "A/btrfs fstab names @ when subvolumes exist"
		grep -q 'subvol=@home' "$_d/target/etc/fstab" \
			&& ok "A/btrfs fstab mounts @home at /home" \
			|| bad "A/btrfs fstab mounts @home at /home"
	fi

	# GRUB config through the helper's own writer.
	# shellcheck disable=SC1090
	( . "$WORK/helper-functions-test.sh"
	  _write_shim_grubcfg "$_uuid_actual" \
		"$_d/target/boot/grub/grub.cfg" "6.12.0-rt" "$_d/target" ) \
		>"$_d/grubcfg.log" 2>&1
	check "A/$_fs grub-script-check accepts grub.cfg" \
		grub-script-check "$_d/target/boot/grub/grub.cfg"
	grep -q "root=UUID=$_uuid_actual" "$_d/target/boot/grub/grub.cfg" \
		&& ok "A/$_fs grub.cfg root=UUID" \
		|| bad "A/$_fs grub.cfg root=UUID"
	case "$_fs" in
	xfs)
		grep -q '^insmod xfs' "$_d/target/boot/grub/grub.cfg" \
			&& ok "A/xfs grub.cfg embeds the xfs module" \
			|| bad "A/xfs grub.cfg embeds the xfs module"
		;;
	btrfs)
		grep -q '^insmod btrfs' "$_d/target/boot/grub/grub.cfg" \
			&& ok "A/btrfs grub.cfg embeds the btrfs module" \
			|| bad "A/btrfs grub.cfg embeds the btrfs module"
		grep -q 'rootflags=subvol=@' "$_d/target/boot/grub/grub.cfg" \
			&& ok "A/btrfs grub.cfg rootflags=subvol=@" \
			|| bad "A/btrfs grub.cfg rootflags=subvol=@"
		;;
	ext4)
		grep -q '^insmod ext2' "$_d/target/boot/grub/grub.cfg" \
			&& ok "A/ext4 grub.cfg embeds the ext2 module" \
			|| bad "A/ext4 grub.cfg embeds the ext2 module"
		;;
	esac

	# EFI install against a directory standing in for the mounted ESP.
	make_foreign_esp "$_d/espdir" "$_d/esp-shared.img"
	_foreign_before="$(esp_dir_digest "$_d/espdir")"
	# shellcheck disable=SC1090
	( . "$WORK/helper-functions-test.sh"
	  _install_grub_efi "$_d/espdir" "$_uuid_actual" "$_d/target" \
		vitruvian 0 ) >"$_d/efi.log" 2>&1 \
		&& ok "A/$_fs EFI install (vitruvian mode)" \
		|| bad "A/$_fs EFI install (vitruvian mode)"
	check "A/$_fs EFI binary is a x86_64 EFI file" \
		grub-file --is-x86_64-efi "$_d/espdir/EFI/Vitruvian/grubx64.efi"
	[ -f "$_d/espdir/EFI/Vitruvian/installed-by-vos" ] \
		&& ok "A/$_fs ESP marker written" \
		|| bad "A/$_fs ESP marker written"
	[ "$(md5sum <"$_d/espdir/EFI/OTHER/other.efi" | awk '{print $1}')" = \
		"$(printf 'foreign-loader\n' | md5sum | awk '{print $1}')" ] \
		&& ok "A/$_fs foreign ESP entry untouched" \
		|| bad "A/$_fs foreign ESP entry untouched"

	# Removable mode must refuse to replace another OS's fallback loader.
	check_not "A/$_fs removable mode refuses foreign fallback" \
		sh -c ". '$WORK/helper-functions-test.sh'; \
			_install_grub_efi '$_d/espdir' '$_uuid_actual' \
			'$_d/target' removable 0"
	check "A/$_fs removable mode with consent replaces only the fallback" \
		sh -c ". '$WORK/helper-functions-test.sh'; \
			_install_grub_efi '$_d/espdir' '$_uuid_actual' \
			'$_d/target' removable 1"
	grep -q foreign-loader "$_d/espdir/EFI/OTHER/other.efi" \
		&& ok "A/$_fs OTHER entry survives allowed clobber" \
		|| bad "A/$_fs OTHER entry survives allowed clobber"

	# BIOS on this GPT disk must refuse: no bios_grub partition.
	_table_snapshot="$(cat "$disk" | md5sum)"
	# shellcheck disable=SC1090
	( . "$WORK/helper-functions-test.sh"
	  _install_grub_bios "$disk" "$_d/target" disk ) \
		>"$_d/bios-refuse.log" 2>&1
	_rc=$?
	[ $_rc -ne 0 ] && grep -q bios_grub "$_d/bios-refuse.log" \
		&& ok "A/$_fs BIOS on GPT without bios_grub refuses" \
		|| bad "A/$_fs BIOS on GPT without bios_grub refuses"
	[ "$(cat "$disk" | md5sum)" = "$_table_snapshot" ] \
		&& ok "A/$_fs refusal leaves the disk untouched" \
		|| bad "A/$_fs refusal leaves the disk untouched"
}

# ------------------------------------- scenario B: shared ESP, other OS
run_scenario_b() {
	_d="$WORK/b-shared"
	mkdir -p "$_d"
	disk="$_d/disk.img"
	truncate -s 4096M "$disk"

	# Hand-built initial layout: ESP, another OS, empty root slot.
	sfdisk "$disk" >/dev/null 2>&1 <<EOF
label: gpt
start=2048, size=1048576, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B
start=1050624, size=2097152, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
start=3147776, size=2097152, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
EOF
	_table_before="$(harness_table_digest "$disk")"

	make_foreign_esp "$_d/espdir" "$_d/esp.img"
	truncate -s 1024M "$_d/other.img"
	mkfs.ext4 -q -F -L otheros "$_d/other.img" >/dev/null 2>&1
	printf 'other-os-canary\n' > "$_d/canary"
	debugfs -w -R "write $_d/canary canary.txt" "$_d/other.img" \
		>/dev/null 2>&1
	dd if="$_d/esp.img" of="$disk" bs=512 seek=2048 conv=notrunc 2>/dev/null
	dd if="$_d/other.img" of="$disk" bs=512 seek=1050624 conv=notrunc \
		2>/dev/null
	_other_md5_before="$(harness_region_md5 "$disk" $((1050624 * 512)) \
		$((2097152 * 512)))"

	truncate -s 1024M "$_d/root.img"
	cat > "$_d/plan.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=format
id=f_root
target_ref=$_d/root.img
filesystem=ext4
label=vitruvian-root
role=root
EOF
	"$WORK/drive-apply.sh" "$_d/plan.txt" >"$_d/apply.log" 2>&1 \
		&& ok "B format root beside another OS" \
		|| bad "B format root beside another OS"
	[ "$(harness_table_digest "$disk")" = "$_table_before" ] \
		&& ok "B partition table untouched by the format plan" \
		|| bad "B partition table untouched by the format plan"
	[ "$(harness_region_md5 "$disk" $((1050624 * 512)) \
		$((2097152 * 512)))" = "$_other_md5_before" ] \
		&& ok "B other OS partition bytes untouched" \
		|| bad "B other OS partition bytes untouched"

	_foreign_before="$(esp_dir_digest "$_d/espdir")"
	_uuid="$(blkid -p -s UUID -o value "$_d/root.img")"
	make_staged_root "$_d/target"
	# shellcheck disable=SC1090
	( . "$WORK/helper-functions-test.sh"
	  _install_grub_efi "$_d/espdir" "$_uuid" "$_d/target" vitruvian 0 ) \
		>"$_d/efi.log" 2>&1 \
		&& ok "B EFI install onto shared ESP" \
		|| bad "B EFI install onto shared ESP"
	check_not "B removable mode refuses to replace foreign fallback" \
		sh -c ". '$WORK/helper-functions-test.sh'; \
			_install_grub_efi '$_d/espdir' '$_uuid' \
			'$_d/target' removable 0"
	[ "$(md5sum <"$_d/espdir/EFI/OTHER/other.efi" | awk '{print $1}')" = \
		"$(printf 'foreign-loader\n' | md5sum | awk '{print $1}')" ] \
		&& ok "B foreign ESP entries byte-identical after install" \
		|| bad "B foreign ESP entries byte-identical after install"
	[ "$(md5sum <"$_d/espdir/EFI/BOOT/BOOTX64.EFI" | awk '{print $1}')" = \
		"$(printf 'foreign-fallback\n' | md5sum | awk '{print $1}')" ] \
		&& ok "B fallback loader byte-identical after install" \
		|| bad "B fallback loader byte-identical after install"

	# The finished root filesystem sits inside the disk at slot 3.
	dd if="$_d/root.img" of="$disk" bs=512 seek=3147776 conv=notrunc \
		2>/dev/null
	[ "$(blkid -p -O $((3147776 * 512)) -s TYPE -o value "$disk")" \
		= "ext4" ] \
		&& ok "B ext4 detectable at its partition offset in the disk" \
		|| bad "B ext4 detectable at its partition offset in the disk"
}

# ------------------------------------------------- scenario C: BIOS / MBR
run_scenario_c() {
	_d="$WORK/c-bios"
	mkdir -p "$_d"
	disk="$_d/disk.img"
	truncate -s 1024M "$disk"

	cat > "$_d/plan.txt" <<EOF
plan_version=1
disk=$disk
table_type=mbr

[op]
kind=create_table
id=t0
type=mbr

[op]
kind=create
id=p_root
start_mib=1
size_mib=-1
gpt_type=linux
role=root
EOF
	"$WORK/drive-apply.sh" "$_d/plan.txt" >"$_d/apply.log" 2>&1 \
		&& ok "C MBR plan apply" \
		|| bad "C MBR plan apply"
	_root_line="$(table_lines "$disk" | sed -n 1p)"
	_root_start="${_root_line%%:*}"
	_root_size="${_root_line#*:}"; _root_size="${_root_size%%:*}"
	truncate -s $((_root_size * 512)) "$_d/root.img"

	cat > "$_d/plan-fmt.txt" <<EOF
plan_version=1
disk=$disk
table_type=mbr

[op]
kind=format
id=f_root
target_ref=$_d/root.img
filesystem=ext4
label=vitruvian-root
role=root
EOF
	"$WORK/drive-apply.sh" "$_d/plan-fmt.txt" >"$_d/apply-fmt.log" 2>&1 \
		&& ok "C root format on MBR layout" \
		|| bad "C root format on MBR layout"
	dd if="$_d/root.img" of="$disk" bs=512 seek="$_root_start" \
		conv=notrunc 2>/dev/null

	_table_before="$(harness_table_digest "$disk")"
	_sig_before="$(dd if="$disk" bs=1 skip=440 count=72 2>/dev/null \
		| md5sum | awk '{print $1}')"
	mkdir -p "$_d/grub"
	write_bios_payload "$disk" "$_d/grub" \
		&& ok "C BIOS payload written (boot.img + core.img)" \
		|| bad "C BIOS payload written (boot.img + core.img)"
	[ "$(harness_table_digest "$disk")" = "$_table_before" ] \
		&& ok "C MBR table survives the BIOS payload" \
		|| bad "C MBR table survives the BIOS payload"
	_sig_after="$(dd if="$disk" bs=1 skip=440 count=72 2>/dev/null \
		| md5sum | awk '{print $1}')"
	[ "$_sig_before" = "$_sig_after" ] \
		&& ok "C disk signature and partition slots preserved" \
		|| bad "C disk signature and partition slots preserved"
	dd if="$disk" of="$_d/sector0.bin" bs=512 count=1 2>/dev/null
	head -c 440 "$_d/sector0.bin" > "$_d/sector0-440.bin"
	head -c 440 /usr/lib/grub/i386-pc/boot.img > "$_d/boot440.bin"
	cmp -s "$_d/sector0-440.bin" "$_d/boot440.bin" \
		&& ok "C boot.img lives at sector 0" \
		|| bad "C boot.img lives at sector 0"
	dd if="$disk" of="$_d/sector1.bin" bs=512 skip=1 count=1 2>/dev/null
	head -c 512 "$_d/grub/core.img" > "$_d/core512.bin"
	cmp -s "$_d/sector1.bin" "$_d/core512.bin" \
		&& ok "C core.img lives at sector 1" \
		|| bad "C core.img lives at sector 1"

	_uuid="$(blkid -p -s UUID -o value "$_d/root.img")"
	make_staged_root "$_d/target"
	# shellcheck disable=SC1090
	( . "$WORK/helper-functions-test.sh"
	  _write_shim_grubcfg "$_uuid" "$_d/target/boot/grub/grub.cfg" \
		"6.12.0-rt" "$_d/target" ) >/dev/null 2>&1
	check "C grub-script-check on BIOS grub.cfg" \
		grub-script-check "$_d/target/boot/grub/grub.cfg"
}

# ----------------------------------------------------- failure injections
run_failures() {
	_d="$WORK/failures"
	mkdir -p "$_d"
	disk="$_d/disk.img"
	truncate -s 512M "$disk"
	_table_before="$(harness_table_digest "$disk")"

	cat > "$_d/bad-fs.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=create_table
id=t0
type=gpt

[op]
kind=format
id=f1
target_ref=$disk
filesystem=notafs
role=root
EOF
	check_not "F invalid filesystem rejected by validate" \
		"$WORK/drive-apply.sh" "$_d/bad-fs.txt"
	[ "$(harness_table_digest "$disk")" = "$_table_before" ] \
		&& ok "F rejected plan leaves the disk untouched" \
		|| bad "F rejected plan leaves the disk untouched"

	cat > "$_d/oversize.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=create
id=p1
start_mib=500
size_mib=100
gpt_type=linux
role=root
EOF
	check_not "F create past end of disk rejected" \
		"$WORK/drive-apply.sh" "$_d/oversize.txt"
	[ "$(harness_table_digest "$disk")" = "$_table_before" ] \
		&& ok "F oversize rejection leaves the disk untouched" \
		|| bad "F oversize rejection leaves the disk untouched"

	# Mid-plan apply failure: the ref exists (so validate passes) but
	# mkfs cannot format a zero-length file.
	: > "$_d/empty.img"
	cat > "$_d/midfail.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=create_table
id=t0
type=gpt

[op]
kind=create
id=p1
start_mib=1
size_mib=64
gpt_type=linux
role=root

[op]
kind=format
id=f1
target_ref=$_d/empty.img
filesystem=ext4
role=root

[op]
kind=set_flags
id=sf1
target_ref=$disk
flag=boot
EOF
	"$WORK/drive-apply.sh" "$_d/midfail.txt" >"$_d/midfail.log" 2>&1
	_rc=$?
	[ $_rc -eq 3 ] && ok "F mid-plan failure exits 3" \
		|| bad "F mid-plan failure exits 3 (rc=$_rc)"
	grep -q '^status=partial' "$_d/midfail.txt.result" \
		&& ok "F result status partial" \
		|| bad "F result status partial"
	grep -q '^status=not_attempted' "$_d/midfail.txt.result" \
		&& ok "F post-failure ops not_attempted" \
		|| bad "F post-failure ops not_attempted"
	sfdisk --dump "$disk" 2>/dev/null | grep -q 'start=' \
		&& ok "F applied prefix (table + create) stays consistent" \
		|| bad "F applied prefix (table + create) stays consistent"

	# Truncated table: the helper must fail cleanly, never crash.
	cp "$disk" "$_d/truncated.img"
	truncate -s 4096 "$_d/truncated.img"
	cat > "$_d/trunc.txt" <<EOF
plan_version=1
disk=$_d/truncated.img
table_type=gpt

[op]
kind=create
id=p9
start_mib=1
size_mib=1
gpt_type=linux
role=root
EOF
	"$WORK/drive-apply.sh" "$_d/trunc.txt" >"$_d/trunc.log" 2>&1
	_rc=$?
	[ $_rc -eq 2 ] || [ $_rc -eq 3 ] \
		&& ok "F truncated disk refused cleanly (rc=$_rc)" \
		|| bad "F truncated disk refused cleanly (rc=$_rc)"

	# Protective MBR without GPT: must not be treated as a normal disk.
	printf 'protective\n' > "$_d/gptless.img"
	truncate -s 128M "$_d/gptless.img"
	sfdisk "$_d/gptless.img" >/dev/null 2>&1 <<EOF
label: dos
start=1, size=262144, type=ee
EOF
	cat > "$_d/gptless.txt" <<EOF
plan_version=1
disk=$_d/gptless.img
table_type=mbr

[op]
kind=create_table
id=t0
type=mbr
EOF
	"$WORK/drive-apply.sh" "$_d/gptless.txt" >"$_d/gptless.log" 2>&1
	_rc=$?
	[ $_rc -eq 0 ] || [ $_rc -eq 3 ] \
		&& ok "F protective-MBR case handled without crash (rc=$_rc)" \
		|| bad "F protective-MBR case handled without crash (rc=$_rc)"

	# Bad superblock: repair check must refuse, not mutate.
	truncate -s 128M "$_d/badfs.img"
	mkfs.ext4 -q -F "$_d/badfs.img" >/dev/null 2>&1
	dd if=/dev/zero of="$_d/badfs.img" bs=1K count=8 conv=notrunc \
		2>/dev/null
	cat > "$_d/badfs.txt" <<EOF
plan_version=1
disk=$disk
table_type=gpt

[op]
kind=repair
id=r1
target_ref=$_d/badfs.img
filesystem=ext4
check_only=1
EOF
	"$WORK/drive-apply.sh" "$_d/badfs.txt" >"$_d/badfs.log" 2>&1
	_rc=$?
	[ $_rc -eq 3 ] && ok "F bad superblock fails cleanly" \
		|| bad "F bad superblock fails cleanly (rc=$_rc)"
}

run_scenario_a ext4
run_scenario_a xfs
run_scenario_a btrfs
run_scenario_b
run_scenario_c
run_failures

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
