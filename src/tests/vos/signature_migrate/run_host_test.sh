#!/bin/sh
# Host test: migrate a fixture settings tree that still uses old signatures.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/fixture"
TOOL="$HERE/vos-signature-migrate-host"
SRC="$HERE/../../../bin/vos-signature-migrate/SignatureMigrate.cpp"

rm -rf "$ROOT"
mkdir -p \
	"$ROOT/home/config/settings/mime_db/application" \
	"$ROOT/home/config/settings/Tracker" \
	"$ROOT/home/config/settings/deskbar/menu/Applications" \
	"$ROOT/home/Desktop" \
	"$ROOT/home/mail/in"

printf 'volwhich=1\n' > "$ROOT/home/config/settings/x-vnd.Haiku-desklink"
printf '# Recent applications\nRecentApp application/x-vnd.Haiku-TextEditor\n' \
	> "$ROOT/home/config/settings/RosterSettings"
printf 'OpenWith application/x-vnd.Haiku-StyledEdit\n' \
	> "$ROOT/home/config/settings/Tracker/TrackerSettings"
printf 'placeholder\n' \
	> "$ROOT/home/config/settings/mime_db/application/x-vnd.haiku-core-file"
printf 'x\n' > "$ROOT/home/Desktop/.keep"
printf 'x\n' > "$ROOT/home/mail/in/.keep"

# Fixture xattrs use the image postinst encoding: 4-byte LE type + value.
python3 - "$ROOT" <<'PY'
import os, struct, sys
root = sys.argv[1]

def set_xattr(path, name, fourcc, value):
    raw = struct.pack("<I", fourcc) + value.encode() + b"\0"
    os.setxattr(path, "user.beos." + name, raw)

CSTR = 0x43535452  # B_STRING_TYPE
MSIG = 0x4D534947

home = os.path.join(root, "home")
set_xattr(os.path.join(home, "Desktop"), "SYS:NAME", CSTR,
          "x-vnd.Haiku-libtracker:B_USER_DIRECTORY:home")
set_xattr(os.path.join(home, "config"), "SYS:NAME", CSTR,
          "x-vnd.Haiku-libtracker:B_USER_CONFIG_DIRECTORY:config")
set_xattr(os.path.join(home, "mail", "in"), "SYS:NAME", CSTR,
          "x-vnd.Be-MAIL:B_USER_DIRECTORY/mail:mail/in")
mime = os.path.join(home, "config", "settings", "mime_db",
                    "application", "x-vnd.haiku-core-file")
set_xattr(mime, "META:PREF_APP", MSIG,
          "application/x-vnd.Haiku-Debugger")
set_xattr(mime, "META:SNIFF_RULE", CSTR,
          "0.91 open with application/x-vnd.Haiku-StyledEdit")
PY

cc -O2 -o "$TOOL" "$SRC"

"$TOOL" --root "$ROOT/home/config/settings"

fail=0
check() {
	if eval "$2"; then
		echo "PASS $1"
	else
		echo "FAIL $1"
		fail=1
	fi
}

get_xattr_value() {
	python3 - "$1" "$2" <<'PY'
import os, sys
name = "user.beos." + sys.argv[2]
try:
    data = os.getxattr(sys.argv[1], name)
except OSError:
    sys.exit(1)
sys.stdout.buffer.write(data[4:])
PY
}

check "renamed signature-keyed settings file" \
	"[ -f '$ROOT/home/config/settings/x-vnd.vos-desklink' ] && [ ! -f '$ROOT/home/config/settings/x-vnd.Haiku-desklink' ]"
check "RosterSettings RecentApp rewritten" \
	"grep -q 'application/x-vnd.vos-TextEditor' '$ROOT/home/config/settings/RosterSettings' && ! grep -q 'x-vnd.Haiku' '$ROOT/home/config/settings/RosterSettings'"
check "Tracker settings rewritten" \
	"grep -q 'application/x-vnd.vos-StyledEdit' '$ROOT/home/config/settings/Tracker/TrackerSettings' && ! grep -q 'x-vnd.Haiku' '$ROOT/home/config/settings/Tracker/TrackerSettings'"
check "mime_db file renamed" \
	"[ -f '$ROOT/home/config/settings/mime_db/application/x-vnd.vos-core-file' ] && [ ! -f '$ROOT/home/config/settings/mime_db/application/x-vnd.haiku-core-file' ]"
check "Desktop SYS:NAME rewritten" \
	"get_xattr_value '$ROOT/home/Desktop' SYS:NAME | grep -q 'x-vnd.vos-libtracker' && ! get_xattr_value '$ROOT/home/Desktop' SYS:NAME | grep -q 'Haiku'"
check "config SYS:NAME rewritten" \
	"get_xattr_value '$ROOT/home/config' SYS:NAME | grep -q 'x-vnd.vos-libtracker'"
check "PREF_APP rewritten" \
	"get_xattr_value '$ROOT/home/config/settings/mime_db/application/x-vnd.vos-core-file' META:PREF_APP | grep -q 'x-vnd.vos-Debugger' && ! get_xattr_value '$ROOT/home/config/settings/mime_db/application/x-vnd.vos-core-file' META:PREF_APP | grep -q 'Haiku'"
check "SNIFF_RULE rewritten" \
	"get_xattr_value '$ROOT/home/config/settings/mime_db/application/x-vnd.vos-core-file' META:SNIFF_RULE | grep -q 'x-vnd.vos-StyledEdit'"
check "marker written" \
	"[ -f '$ROOT/home/config/settings/vos-signature-migrated' ]"
check "mail folder Be-MAIL left unchanged" \
	"get_xattr_value '$ROOT/home/mail/in' SYS:NAME | grep -q 'x-vnd.Be-MAIL'"

"$TOOL" --root "$ROOT/home/config/settings"
check "second run keeps new names" \
	"[ -f '$ROOT/home/config/settings/x-vnd.vos-desklink' ] && ! grep -rq 'x-vnd.Haiku' '$ROOT/home/config/settings' 2>/dev/null"

if [ "$fail" -ne 0 ]; then
	echo "HOST TEST FAILED"
	exit 1
fi
echo "HOST TEST PASSED"
