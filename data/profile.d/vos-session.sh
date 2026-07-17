# Vitruvian TTY-intermediate login hook.
#
# Started by /etc/profile at the end of an interactive login shell. If
# the user has a graphics-capable seat (systemd-logind assigns one when
# logging in on a VT), start the user-scoped vos-session unit.
#
# Not active by default — /etc/profile.d/ inclusion requires the postinst
# to enable it (kernel cmdline vitruvian.autologin=1 disables this path).

if [ -z "$VOS_SESSION_STARTED" ] && \
		[ -n "$XDG_SESSION_ID" ] && \
		[ "$(id -u)" != "0" ] && \
		[ -x /usr/bin/systemctl ]; then
	if systemctl --user is-enabled vos-session.service >/dev/null 2>&1; then
		export VOS_SESSION_STARTED=1
		systemctl --user --no-block start vos-session.service
	fi
fi
