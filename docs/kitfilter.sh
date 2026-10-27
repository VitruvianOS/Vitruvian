#!/bin/sh
# Doxygen INPUT_FILTER: put every header in headers/os/<dir>/ into the kit
# group kit_<dir> defined in kits.dox, so the real headers stay untouched.
# The opening comment needs its own line, some headers start with #ifndef.
f=$1
kit=$(printf '%s\n' "$f" | sed -n 's#.*headers/os/\([^/]*\)/.*#\1#p' | tr -c 'a-z0-9\n' '_')
if [ -z "$kit" ]; then
	cat "$f"
	exit
fi
printf '/** \\addtogroup kit_%s */ /** @{ */\n' "$kit"
cat "$f"
printf '\n/** @} */\n'
