find_library(PAM_LIBRARIES NAMES pam REQUIRED)
find_path(PAM_INCLUDE_DIRS NAMES security/pam_appl.h REQUIRED)
find_library(SYSTEMD_LIBRARIES NAMES systemd REQUIRED)
find_path(SYSTEMD_INCLUDE_DIRS NAMES systemd/sd-login.h REQUIRED)

add_executable(vos-polkit-grant-helper
	vos-polkit-grant-helper.cpp
	grant_store.cpp
)

target_include_directories(vos-polkit-grant-helper PRIVATE
	${PAM_INCLUDE_DIRS}
	${SYSTEMD_INCLUDE_DIRS}
)
target_link_libraries(vos-polkit-grant-helper PRIVATE
	${PAM_LIBRARIES}
	${SYSTEMD_LIBRARIES}
)

# Setuid root, same trust shape as polkit-agent-helper-1: the password
# arrives on stdin and PAM decides. The image pipeline must preserve the
# setuid bit (dpkg does; verify on the installed system).
install(TARGETS vos-polkit-grant-helper DESTINATION /usr/libexec
	PERMISSIONS
	OWNER_READ OWNER_WRITE OWNER_EXECUTE
	GROUP_READ GROUP_EXECUTE
	WORLD_READ WORLD_EXECUTE
	SETUID
)
