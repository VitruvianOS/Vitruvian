find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

Application(vos-polkit-agent

	SOURCES
	vos-polkit-agent.cpp

	LIBS be ${SYSTEMD_LIBRARIES}

	RDEF vos-polkit-agent.rdef
)

target_include_directories(vos-polkit-agent PRIVATE ${SYSTEMD_INCLUDE_DIRS})
UsePrivateHeaders(vos-polkit-agent app)
