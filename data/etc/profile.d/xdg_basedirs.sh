#
# Vitruvian setup for the
# XDG Base Directory Specification
#
# http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
#
# Sourced by /system/settings/etc/profile for login shells: Terminal, SSH, etc.


# defaults to ~/.config
_vos_dir="`finddir B_USER_SETTINGS_DIRECTORY 2>/dev/null`"
[ -n "$_vos_dir" ] && export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$_vos_dir}"

# defaults to ~/.local/share
_vos_dir="`finddir B_USER_DATA_DIRECTORY 2>/dev/null`"
[ -n "$_vos_dir" ] && export XDG_DATA_HOME="${XDG_DATA_HOME:-$_vos_dir}"

# defaults to ~/.cache
_vos_dir="`finddir B_USER_CACHE_DIRECTORY 2>/dev/null`"
[ -n "$_vos_dir" ] && export XDG_CACHE_HOME="${XDG_CACHE_HOME:-$_vos_dir}"

# defaults to /etc/xdg
_vos_dir="`finddir B_SYSTEM_SETTINGS_DIRECTORY 2>/dev/null`"
[ -n "$_vos_dir" ] && export XDG_CONFIG_DIRS="${XDG_CONFIG_DIRS:-$_vos_dir:/etc/xdg}"

# defaults to /usr/local/share/:/usr/share/
_vos_dir="`finddir B_SYSTEM_DATA_DIRECTORY 2>/dev/null`"
[ -n "$_vos_dir" ] && export XDG_DATA_DIRS="${XDG_DATA_DIRS:-$_vos_dir:/usr/local/share:/usr/share}"

unset _vos_dir
