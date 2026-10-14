find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

Application(vos-polkit-agent

	SOURCES
	vos-polkit-agent.cpp
	../vos-polkit-grant-helper/grant_store.cpp

	LIBS be shared localestub ${SYSTEMD_LIBRARIES}

	RDEF vos-polkit-agent.rdef
)

target_include_directories(vos-polkit-agent PRIVATE
	${SYSTEMD_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}/../vos-polkit-grant-helper
)
UsePrivateHeaders(vos-polkit-agent app shared)
DoCatalogs("application/x-vnd.Vitruvian-polkit-agent" apps/vos-polkit-agent)
