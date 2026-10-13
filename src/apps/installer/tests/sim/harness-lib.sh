#!/bin/sh
# Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
# All rights reserved. Distributed under the terms of the MIT License.
#
# Test-only slice of vos-install-helper (function definitions, root gate
# and dispatch dropped, file-disk checks widened). Mechanical extraction
# of the one shipping file, never installed, never a second code path.

HARNESS_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
HARNESS_SRC_ROOT="$(CDPATH= cd -- "$HARNESS_DIR/../../../../.." && pwd)"
HARNESS_HELPER="$HARNESS_SRC_ROOT/data/libexec/vos-install-helper"
HARNESS_LIB="$HARNESS_SRC_ROOT/data/libexec/vos-partition-lib.sh"

# Extracts helper function definitions, then widens the shipped front
# door (block-device-only by design) to accept the harness's file disks.
harness_extract() {
	_out="$1"
	awk '
		/^if \[ "\$\(id -u\)" -ne 0 \]; then/ { skip=1 }
		skip && /^fi$/ { skip=0; next }
		skip { next }
		/^# ============================== dispatch/ { exit }
		{ print }
	' "$HARNESS_HELPER" > "$_out"

	# File disks: accept absolute paths and regular files.
	sed -i \
		-e 's|/dev/\[A-Za-z0-9_/-\]\*) ;;|/dev/[A-Za-z0-9_/-]*\|/*) ;;|' \
		-e 's|/dev/\[A-Za-z0-9_/-\]\*) return 0 ;;|/dev/[A-Za-z0-9_/-]*\|/*) return 0 ;;|' \
		-e 's#\[ -b "\$_plan_disk" \] ||#[ -b "$_plan_disk" ] || [ -f "$_plan_disk" ] ||#' \
		-e 's#\[ ! -b "\$_tref" \]#[ ! -b "$_tref" ] \&\& [ ! -f "$_tref" ]#g' \
		"$_out"

	# lsblk cannot size a file; fall back to wc -c.
	sed -i 's#_disk_size_mib=$(( $(lsblk -bdno SIZE "$_plan_disk" 2>/dev/null || echo 0) / 1048576 ))#_disk_size_mib=$(( $(lsblk -bdno SIZE "$_plan_disk" 2>/dev/null || echo 0) / 1048576 ))\n    if [ "$_disk_size_mib" -le 0 ] \&\& [ -f "$_plan_disk" ]; then\n        _disk_size_mib=$(( $(wc -c < "$_plan_disk") / 1048576 ))\n    fi#' \
		"$_out"

	# The shipped file sources the lib next to itself, falling back to
	# the install path; pin the fallback to the tree copy so sourcing
	# from a test subshell (where $0 is not the driver) still works.
	sed -i "s#^    \. /usr/libexec/vos-partition-lib.sh#    . \"$HARNESS_LIB\"#" \
		"$_out"

	ln -sf "$HARNESS_LIB" "$(dirname -- "$_out")/vos-partition-lib.sh"
}

# Driver that applies a plan through the extracted functions.
harness_write_apply_driver() {
	_out="$1"
	cat > "$_out" <<'EOF'
#!/bin/sh
set -eu
. "$(dirname -- "$0")/helper-functions-test.sh"
_do_partition_apply "$@"
EOF
	chmod +x "$_out"
}

# Regions of a disk image outside the plan (start byte, size bytes pairs)
# are checksummed before and after; anything that moves is an invariant
# break unless the plan named it.
harness_region_md5() {
	_img="$1"; _start="$2"; _size="$3"
	dd if="$_img" bs=512 skip="$((_start / 512))" \
		count="$((_size / 512))" 2>/dev/null | md5sum | awk '{print $1}'
}

# sfdisk partition table digest: label plus every entry, whitespace flat.
harness_table_digest() {
	sfdisk --dump "$1" 2>/dev/null | sed 's/[[:space:]]\+/ /g' \
		| sed '/^$/d' | md5sum | awk '{print $1}'
}
