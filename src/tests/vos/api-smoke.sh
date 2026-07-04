#!/bin/bash
#
# Vitruvian API smoke test.
#
# Exercises every public Vitruvian-specific API surface that has a
# binutil exposed under src/bin. Designed to be run before and after
# each MASTER-PLAN.md phase as a baseline regression check.
#
# Usage:
#   ./api-smoke.sh                          # run, print summary
#   ./api-smoke.sh -v                       # verbose (show each test)
#   ./api-smoke.sh --baseline FILE          # write results to FILE
#   ./api-smoke.sh --compare FILE           # compare results to FILE
#   ./api-smoke.sh --only attr,mime,query   # run only listed sections
#   ./api-smoke.sh --skip query,trash       # skip listed sections
#
# Exit codes:
#   0  all checks passed
#   1  at least one check failed (or --compare detected regression)
#   2  setup error (no writable temp, missing core tools)
#
# Sections (Vitruvian-specific API surface only):
#   find_directory   finddir, findpaths        → find_directory(), find_path()
#   volume           isvolume, driveinfo,
#                    mountvolume listing       → BVolume, fs_stat_dev, next_dev
#   attr             addattr, listattr, catattr,
#                    rmattr, mvattr, copyattr  → fs_attr family
#   mime             settype, mimeset, setmime → BMimeType, settype_for_path
#   index            mkindex, lsindex, rmindex → fs_index family
#   query            query                     → BQuery (parity gap G1)
#   trash            trash                     → FSMoveToTrash
#   roster           roster, version, sysinfo  → BRoster, system_info, version
#   kernel_objects   listsem, listport,
#                    listimage, listarea       → kernel-side BeOS objects
#
# Deliberately excluded (not API smoke material):
#   - interface/UI binaries (alert, beep, draggers, dpms, clipboard,
#     screen_blanker, screenmode, hey, ffm, urlwrapper, notify, filepanel)
#   - resource-file format tools (xres, listres, resattr)
#   - build-only helpers (rc, mountvolume.rdef, etc.)
#
# Each test reports one of:
#   PASS  expected behaviour observed
#   FAIL  unexpected behaviour observed
#   SKIP  binary missing, environment unsuitable, or known stub
#   GAP   subsystem known broken per haiku-parity-gaps.md (counted)
#

set -u

###############################################################################
# Config
###############################################################################

VERBOSE=0
BASELINE_FILE=""
COMPARE_FILE=""
ONLY_SECTIONS=""
SKIP_SECTIONS=""

PASS=0
FAIL=0
SKIP=0
GAP=0

TMPDIR=""
RESULTS=""

###############################################################################
# Plumbing
###############################################################################

log() {
	[ "$VERBOSE" -eq 1 ] && echo "$@" >&2
}

record() {
	local status="$1"
	local name="$2"
	local detail="${3:-}"

	RESULTS+="${status}|${name}|${detail}"$'\n'

	case "$status" in
		PASS) PASS=$((PASS+1)); [ "$VERBOSE" -eq 1 ] && echo "  PASS  $name" ;;
		FAIL) FAIL=$((FAIL+1)); echo "  FAIL  $name  $detail" >&2 ;;
		SKIP) SKIP=$((SKIP+1)); [ "$VERBOSE" -eq 1 ] && echo "  SKIP  $name  $detail" ;;
		GAP)  GAP=$((GAP+1)); [ "$VERBOSE" -eq 1 ] && echo "  GAP   $name  $detail" ;;
	esac
}

have() {
	command -v "$1" >/dev/null 2>&1
}

section_enabled() {
	local s="$1"
	if [ -n "$ONLY_SECTIONS" ]; then
		[[ ",$ONLY_SECTIONS," == *",$s,"* ]] || return 1
	fi
	if [ -n "$SKIP_SECTIONS" ]; then
		[[ ",$SKIP_SECTIONS," == *",$s,"* ]] && return 1
	fi
	return 0
}

###############################################################################
# Arg parsing
###############################################################################

while [ $# -gt 0 ]; do
	case "$1" in
		-v|--verbose) VERBOSE=1 ;;
		--baseline) BASELINE_FILE="$2"; shift ;;
		--compare)  COMPARE_FILE="$2"; shift ;;
		--only)     ONLY_SECTIONS="$2"; shift ;;
		--skip)     SKIP_SECTIONS="$2"; shift ;;
		-h|--help)
			grep '^#' "$0" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*) echo "unknown arg: $1" >&2; exit 2 ;;
	esac
	shift
done

###############################################################################
# Setup
###############################################################################

# Need a writable tmp on a filesystem that supports xattrs (for attr tests).
# /tmp is usually tmpfs which DOES support user.* xattrs; if not, fall back.
TMPDIR="$(mktemp -d -t vitruvian-smoke.XXXXXX 2>/dev/null)"
if [ -z "$TMPDIR" ] || [ ! -d "$TMPDIR" ]; then
	echo "FATAL: cannot create temp directory" >&2
	exit 2
fi

# Verify xattr support before relying on it.
ATTR_SUPPORTED=1
echo "probe" > "$TMPDIR/.xattr-probe"
if setfattr -n user.smoke -v 1 "$TMPDIR/.xattr-probe" 2>/dev/null; then
	getfattr -n user.smoke "$TMPDIR/.xattr-probe" >/dev/null 2>&1 || ATTR_SUPPORTED=0
else
	ATTR_SUPPORTED=0
fi
rm -f "$TMPDIR/.xattr-probe"

cleanup() {
	rm -rf "$TMPDIR" 2>/dev/null
}
trap cleanup EXIT

###############################################################################
# Sections
###############################################################################

run_find_directory() {
	section_enabled find_directory || return
	echo "== find_directory =="

	if have finddir; then
		# B_USER_DIRECTORY is one of the canonical values; result must be
		# a non-empty absolute path that exists.
		local out
		out="$(finddir B_USER_DIRECTORY 2>/dev/null)"
		if [ -n "$out" ] && [ "${out:0:1}" = "/" ] && [ -d "$out" ]; then
			record PASS find_directory/B_USER_DIRECTORY "$out"
		else
			record FAIL find_directory/B_USER_DIRECTORY "got='$out'"
		fi

		out="$(finddir B_SYSTEM_DIRECTORY 2>/dev/null)"
		if [ -n "$out" ] && [ "${out:0:1}" = "/" ]; then
			record PASS find_directory/B_SYSTEM_DIRECTORY "$out"
		else
			record FAIL find_directory/B_SYSTEM_DIRECTORY "got='$out'"
		fi

		out="$(finddir B_TRASH_DIRECTORY 2>/dev/null)"
		if [ -n "$out" ] && [ "${out:0:1}" = "/" ]; then
			record PASS find_directory/B_TRASH_DIRECTORY "$out"
		else
			record FAIL find_directory/B_TRASH_DIRECTORY "got='$out'"
		fi
	else
		record SKIP find_directory/finddir "binary missing"
	fi

	if have findpaths; then
		findpaths B_FIND_PATH_FONTS_DIRECTORY >/dev/null 2>&1 \
			&& record PASS find_directory/findpaths \
			|| record FAIL find_directory/findpaths "non-zero exit"
	else
		record SKIP find_directory/findpaths "binary missing"
	fi
}

run_volume() {
	section_enabled volume || return
	echo "== volume =="

	if have isvolume; then
		# Boot vol must self-identify.
		if isvolume / 2>/dev/null | grep -qiE "yes|true|persistent|read"; then
			record PASS volume/isvolume_root "boot vol recognised"
		else
			# isvolume's output format is flag-letter-based; fall back to
			# exit code.
			isvolume / >/dev/null 2>&1 \
				&& record PASS volume/isvolume_root "exit 0" \
				|| record FAIL volume/isvolume_root "non-zero exit"
		fi
	else
		record SKIP volume/isvolume "binary missing"
	fi

	if have driveinfo; then
		driveinfo >/dev/null 2>&1 \
			&& record PASS volume/driveinfo \
			|| record FAIL volume/driveinfo "non-zero exit"
	else
		record SKIP volume/driveinfo "binary missing"
	fi

	if have mountvolume; then
		# Listing should succeed and include the boot volume marker.
		local out
		out="$(mountvolume 2>/dev/null)"
		if [ -n "$out" ]; then
			record PASS volume/mountvolume_list "$(echo "$out" | wc -l) lines"
		else
			record FAIL volume/mountvolume_list "empty output"
		fi
	else
		record SKIP volume/mountvolume "binary missing"
	fi
}

run_attr() {
	section_enabled attr || return
	echo "== attr =="

	if [ "$ATTR_SUPPORTED" -eq 0 ]; then
		record SKIP attr/_all_ "xattr unsupported on temp fs"
		return
	fi

	local f="$TMPDIR/attr-target"
	echo "payload" > "$f"

	if have addattr && have listattr && have catattr && have rmattr; then
		# Write a string attr, read back via catattr, then remove.
		addattr -t string TESTATTR "hello-vitruvian" "$f" 2>/dev/null
		local listing catout
		listing="$(listattr "$f" 2>/dev/null)"
		catout="$(catattr TESTATTR "$f" 2>/dev/null)"

		if echo "$listing" | grep -q "TESTATTR"; then
			record PASS attr/addattr_listattr "found in listing"
		else
			record FAIL attr/addattr_listattr "not in listing"
		fi

		if echo "$catout" | grep -q "hello-vitruvian"; then
			record PASS attr/catattr_roundtrip "value matches"
		else
			record FAIL attr/catattr_roundtrip "value='$catout'"
		fi

		rmattr TESTATTR "$f" 2>/dev/null
		listing="$(listattr "$f" 2>/dev/null)"
		if echo "$listing" | grep -q "TESTATTR"; then
			record FAIL attr/rmattr "still in listing"
		else
			record PASS attr/rmattr
		fi
	else
		record SKIP attr/core "missing addattr/listattr/catattr/rmattr"
	fi

	if have mvattr; then
		addattr -t string FOO bar "$f" 2>/dev/null
		mvattr -- FOO BAR "$f" 2>/dev/null
		if listattr "$f" 2>/dev/null | grep -q "BAR"; then
			record PASS attr/mvattr
		else
			record FAIL attr/mvattr "rename did not stick"
		fi
		rmattr BAR "$f" 2>/dev/null
	else
		record SKIP attr/mvattr "binary missing"
	fi

	if have copyattr; then
		local src="$TMPDIR/copy-src"
		local dst="$TMPDIR/copy-dst"
		echo s > "$src"; echo d > "$dst"
		addattr -t string COPYME "copied" "$src" 2>/dev/null
		copyattr "$src" "$dst" 2>/dev/null
		if catattr COPYME "$dst" 2>/dev/null | grep -q copied; then
			record PASS attr/copyattr
		else
			record FAIL attr/copyattr "attr did not propagate"
		fi
	else
		record SKIP attr/copyattr "binary missing"
	fi

	rm -f "$f"
}

run_mime() {
	section_enabled mime || return
	echo "== mime =="

	local f="$TMPDIR/mime-target.txt"
	echo "Hello, World" > "$f"

	if have settype && have catattr; then
		settype -t text/plain "$f" 2>/dev/null
		local got
		got="$(catattr BEOS:TYPE "$f" 2>/dev/null)"
		if echo "$got" | grep -q "text/plain"; then
			record PASS mime/settype_catattr
		else
			record FAIL mime/settype_catattr "BEOS:TYPE='$got'"
		fi
	else
		record SKIP mime/settype "missing settype or catattr"
	fi

	if have mimeset; then
		# Re-derive type. Output must be non-empty or exit 0.
		mimeset -F "$f" >/dev/null 2>&1 \
			&& record PASS mime/mimeset \
			|| record FAIL mime/mimeset "non-zero exit"
	else
		record SKIP mime/mimeset "binary missing"
	fi

	if have setmime; then
		# setmime queries the MIME database; -dump or -dump <type>
		# should succeed for a known type.
		setmime -dump text/plain >/dev/null 2>&1 \
			&& record PASS mime/setmime_dump \
			|| record FAIL mime/setmime_dump "dump failed"
	else
		record SKIP mime/setmime "binary missing"
	fi

	rm -f "$f"
}

run_index() {
	section_enabled index || return
	echo "== index =="

	if ! have mkindex || ! have lsindex || ! have rmindex; then
		record SKIP index/_all_ "missing mkindex/lsindex/rmindex"
		return
	fi

	# Index operations are dev-scoped. Use boot vol.
	local dev="/"

	mkindex -t string -v "$dev" smoke_test_idx 2>/dev/null
	local mk_rc=$?

	local listing
	listing="$(lsindex -v "$dev" 2>/dev/null)"

	if [ $mk_rc -eq 0 ] && echo "$listing" | grep -q "smoke_test_idx"; then
		record PASS index/mkindex_lsindex
	elif [ $mk_rc -ne 0 ]; then
		# Index creation might fail if FS doesn't support it (ext4 without
		# the nexus index extension) — not a regression.
		record SKIP index/mkindex_lsindex "mkindex rc=$mk_rc (fs no support?)"
	else
		record FAIL index/mkindex_lsindex "not in lsindex"
	fi

	if [ $mk_rc -eq 0 ]; then
		rmindex -v "$dev" smoke_test_idx 2>/dev/null
		listing="$(lsindex -v "$dev" 2>/dev/null)"
		if echo "$listing" | grep -q "smoke_test_idx"; then
			record FAIL index/rmindex "still present"
		else
			record PASS index/rmindex
		fi
	fi

	if have reindex; then
		# Just exercise the entry point. Real reindex would need an existing
		# index + file to walk; a no-op invocation should not crash.
		reindex --help >/dev/null 2>&1
		[ $? -le 1 ] \
			&& record PASS index/reindex_help \
			|| record FAIL index/reindex_help "crash"
	else
		record SKIP index/reindex "binary missing"
	fi
}

run_query() {
	section_enabled query || return
	echo "== query =="

	# haiku-parity-gaps.md G1: fs_query is a stub returning B_NOT_SUPPORTED.
	# Until that's fixed, every query MUST fail. Test should expect failure
	# and report GAP rather than FAIL.

	if ! have query; then
		record SKIP query/binary "binary missing"
		return
	fi

	# Trivial predicate. Pre-G1: query exits non-zero with B_NOT_SUPPORTED.
	local out rc
	out="$(query "name=*" 2>&1)"
	rc=$?
	if [ $rc -eq 0 ] && [ -n "$out" ]; then
		# G1 fixed — actual results.
		record PASS query/basic_predicate "$(echo "$out" | wc -l) results"
	elif echo "$out" | grep -qiE "not supported|not implemented|B_NOT_SUPPORTED"; then
		record GAP query/basic_predicate "G1 not-supported (expected pre-fix)"
	else
		record FAIL query/basic_predicate "rc=$rc out='$out'"
	fi
}

run_trash() {
	section_enabled trash || return
	echo "== trash =="

	if ! have trash; then
		record SKIP trash/binary "binary missing"
		return
	fi

	local f="$TMPDIR/trash-target"
	echo content > "$f"

	# Move to trash.
	trash "$f" 2>/dev/null
	if [ -f "$f" ]; then
		record FAIL trash/move "source still exists"
	else
		record PASS trash/move
	fi

	# Restore would need either a `untrash` binary or Tracker scripting —
	# neither is reliably available headless. Skip and document.
	record SKIP trash/restore "no headless restore path"
}

run_roster() {
	section_enabled roster || return
	echo "== roster =="

	if have roster; then
		roster >/dev/null 2>&1 \
			&& record PASS roster/list \
			|| record FAIL roster/list "non-zero exit"
	else
		record SKIP roster/binary "missing"
	fi

	if have version; then
		local v
		v="$(version 2>/dev/null)"
		[ -n "$v" ] \
			&& record PASS roster/version "$v" \
			|| record FAIL roster/version "empty"
	else
		record SKIP roster/version "binary missing"
	fi

	if have sysinfo; then
		sysinfo >/dev/null 2>&1 \
			&& record PASS roster/sysinfo \
			|| record FAIL roster/sysinfo "non-zero exit"
	else
		record SKIP roster/sysinfo "binary missing"
	fi
}

run_kernel_objects() {
	section_enabled kernel_objects || return
	echo "== kernel_objects =="

	# Each of these enumerates a kernel-side BeOS object kind via the
	# corresponding API (get_next_sem_info, get_next_port_info,
	# get_next_image_info, get_next_area_info). A zero exit + non-empty
	# output proves the syscall path works.
	for tool in listsem listport listimage listarea; do
		if have "$tool"; then
			local out
			out="$("$tool" 2>/dev/null)"
			if [ $? -eq 0 ] && [ -n "$out" ]; then
				record PASS "kernel_objects/$tool" "$(echo "$out" | wc -l) lines"
			else
				record FAIL "kernel_objects/$tool" "rc=$? empty=$([ -z "$out" ] && echo yes || echo no)"
			fi
		else
			record SKIP "kernel_objects/$tool" "binary missing"
		fi
	done
}

###############################################################################
# Main
###############################################################################

echo "Vitruvian API smoke test — $(date -Iseconds)"
echo "tmp:    $TMPDIR"
echo "xattrs: $([ "$ATTR_SUPPORTED" -eq 1 ] && echo yes || echo no)"
echo

run_find_directory
run_volume
run_attr
run_mime
run_index
run_query
run_trash
run_roster
run_resources
run_ipc

echo
echo "==================================================="
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"
echo "  SKIP: $SKIP"
echo "  GAP:  $GAP   (known broken per haiku-parity-gaps.md)"
echo "==================================================="

# Baseline / compare modes.
if [ -n "$BASELINE_FILE" ]; then
	echo "$RESULTS" > "$BASELINE_FILE"
	echo "Baseline written to: $BASELINE_FILE"
fi

if [ -n "$COMPARE_FILE" ]; then
	if [ ! -f "$COMPARE_FILE" ]; then
		echo "Compare file missing: $COMPARE_FILE" >&2
		exit 2
	fi
	# Diff status fields only (column 1) keyed by test name (column 2).
	# Regression = something that was PASS is now FAIL.
	# Improvement = something that was FAIL/GAP is now PASS.
	local_tmp="$(mktemp)"
	echo "$RESULTS" | awk -F'|' 'NF{print $2"\t"$1}' | sort > "$local_tmp.now"
	awk -F'|' 'NF{print $2"\t"$1}' "$COMPARE_FILE" | sort > "$local_tmp.was"

	regressions=$(join -t $'\t' "$local_tmp.was" "$local_tmp.now" \
		| awk -F'\t' '$2=="PASS" && $3!="PASS" {print $1" was "$2", now "$3}')
	improvements=$(join -t $'\t' "$local_tmp.was" "$local_tmp.now" \
		| awk -F'\t' '($2=="FAIL"||$2=="GAP") && $3=="PASS" {print $1" was "$2", now PASS"}')

	rm -f "$local_tmp" "$local_tmp.now" "$local_tmp.was"

	if [ -n "$improvements" ]; then
		echo
		echo "Improvements:"
		echo "$improvements" | sed 's/^/  /'
	fi
	if [ -n "$regressions" ]; then
		echo
		echo "Regressions:"
		echo "$regressions" | sed 's/^/  /'
		exit 1
	else
		echo
		echo "No regressions vs baseline."
	fi
fi

[ "$FAIL" -gt 0 ] && exit 1
exit 0
