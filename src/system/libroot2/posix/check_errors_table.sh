#!/bin/sh
# Fails when a status_t code defined in Errors.h has no table row.
set -e

ERRORS_H="${1:-headers/os/support/Errors.h}"
TABLE="${2:-src/system/libroot2/posix/errors.cpp}"

if [ ! -f "$ERRORS_H" ]; then
	echo "check_errors_table: missing $ERRORS_H" >&2
	exit 2
fi
if [ ! -f "$TABLE" ]; then
	echo "check_errors_table: missing $TABLE" >&2
	exit 2
fi

missing=0
for name in $(grep -oE '#define[[:space:]]+B_[A-Z0-9_]+' "$ERRORS_H" \
	| awk '{print $2}' | sort -u); do
	if ! grep -q "$name" "$TABLE"; then
		echo "MISSING table entry: $name"
		missing=1
	fi
done

if [ "$missing" -ne 0 ]; then
	echo "check_errors_table: FAIL" >&2
	exit 1
fi

count=$(grep -cE '^[[:space:]]*\{[[:space:]]*B_' "$TABLE" || true)
echo "check_errors_table: OK ($count table rows cover all B_* codes in $ERRORS_H)"
