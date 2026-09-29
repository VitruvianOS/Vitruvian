 #  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

# install() helpers: all targets use the "runtime" component so
# CPACK_DEB_COMPONENT_INSTALL can split them cleanly from "dev".

function( ImageInclude path )
	foreach(arg IN LISTS ARGN)
		install(TARGETS ${arg}
			COMPONENT runtime
			ARCHIVE DESTINATION ${path}
			RUNTIME DESTINATION ${path}
			LIBRARY DESTINATION ${path}
			DESTINATION ${path}
			# Tracker's add-on enumerator and BTranslatorRoster require
			# the +x bit (IsExecutable filter in LoadAddOnDir). CMake's
			# default for shared libraries is 644.
			PERMISSIONS
				OWNER_READ OWNER_WRITE OWNER_EXECUTE
				GROUP_READ GROUP_EXECUTE
				WORLD_READ WORLD_EXECUTE
		)
	endforeach()
endfunction()

function( ImageIncludeFile source dest )
	install(FILES ${source} DESTINATION ${dest} COMPONENT runtime)
endfunction()

function( ImageIncludeDir source dest )
	# Preserve +x on shipped scripts (e.g. /system/boot/first_login/*).
	install(DIRECTORY ${source} DESTINATION ${dest} USE_SOURCE_PERMISSIONS
		COMPONENT runtime)
endfunction()

function( ImageCreateDir dest )
	install(DIRECTORY DESTINATION ${dest} COMPONENT runtime)
endfunction()

include(build/baseimage.cmake)
include(build/profiles/base.cmake)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
include(build/profiles/debug.cmake)
endif()

include(build/profiles/data.cmake)

string(REPLACE ";" "," RESULT "${RUN_LIST}")

set(CORE_DEPS "${RESULT}")

execute_process(
	COMMAND git rev-parse --short HEAD
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE VOS_GIT_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)

if(DEFINED ENV{VOS_SOURCE_DATE_EPOCH} AND NOT "$ENV{VOS_SOURCE_DATE_EPOCH}" STREQUAL "")
	set(VOS_SOURCE_DATE_EPOCH "$ENV{VOS_SOURCE_DATE_EPOCH}")
else()
	execute_process(
		COMMAND git log -1 --format=%ct
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		OUTPUT_VARIABLE VOS_SOURCE_DATE_EPOCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
endif()
if(VOS_SOURCE_DATE_EPOCH)
	set(CPACK_DEB_PACKAGE_MTIME "${VOS_SOURCE_DATE_EPOCH}")
	# Some CPack internals read the env, not the variable.
	set(ENV{SOURCE_DATE_EPOCH} "${VOS_SOURCE_DATE_EPOCH}")
	message(STATUS "VOS SOURCE_DATE_EPOCH: ${VOS_SOURCE_DATE_EPOCH}")
else()
	message(WARNING "no git commit timestamp available - package build will not be byte-reproducible")
endif()

if(DEFINED ENV{VOS_PKG_REVISION} AND NOT "$ENV{VOS_PKG_REVISION}" STREQUAL "")
	set(VOS_PKG_REVISION "$ENV{VOS_PKG_REVISION}")
else()
	set(VOS_PKG_REVISION "1")
endif()
# '-' would be ambiguous (dpkg splits on the last hyphen); anything outside
# Debian's alphabet is rejected by reprepro after the build already ran.
if(NOT VOS_PKG_REVISION MATCHES "^[0-9A-Za-z.+~]+$")
	message(FATAL_ERROR "VOS_PKG_REVISION='${VOS_PKG_REVISION}' is not a valid Debian packaging revision ([0-9A-Za-z.+~]+)")
endif()
if(VOS_GIT_SHA)
	set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}+git${VOS_GIT_SHA}-${VOS_PKG_REVISION}")
	message(STATUS "VOS package version: ${CPACK_DEBIAN_PACKAGE_VERSION}")
else()
	message(WARNING "no git sha available - package version stays ${PROJECT_VERSION}, which collides in the pool on the next rebuild")
endif()

# Two DEB components: runtime -> vos.deb (OS image), dev -> vos-dev.deb
# (public headers, link libraries, vos.pc).

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${VITRUVIAN_TARGET_ARCH})
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Vitruvian Project")
SET(CPACK_DEB_COMPONENT_INSTALL ON)

# CPackDeb uppercases the component when it looks these up; spelled in
# lower case they are silently ignored and the debs come out named
# vos-runtime / vos-dev-dev.
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "vos")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_DEPENDS "${CORE_DEPS}")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_CONTROL_EXTRA
	"${CMAKE_CURRENT_SOURCE_DIR}/data/debian/postinst"
	"${CMAKE_CURRENT_SOURCE_DIR}/data/debian/prerm"
	"${CMAKE_CURRENT_SOURCE_DIR}/data/debian/postrm")

# CPACK_PACKAGE_VERSION is only defined once CPack itself is included, so
# the strict dependency has to name the version computed above.
if(CPACK_DEBIAN_PACKAGE_VERSION)
	set(_vos_runtime_version "${CPACK_DEBIAN_PACKAGE_VERSION}")
else()
	set(_vos_runtime_version "${PROJECT_VERSION}")
endif()

set(CPACK_DEBIAN_DEV_PACKAGE_NAME "vos-dev")
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "vos (= ${_vos_runtime_version})")
set(CPACK_DEBIAN_DEV_PACKAGE_DESCRIPTION
	"V\\:OS development files: public headers, libraries, and pkg-config")

# Make `ninja clean` (and `make clean`) wipe CPack outputs too. CPack writes
# its artifacts into the build root, so they normally survive `clean` and
# accumulate stale .deb files across reconfigurations.
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
	ADDITIONAL_CLEAN_FILES
		"${CMAKE_BINARY_DIR}/_CPack_Packages"
		"${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.deb"
		"${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}-runtime.deb"
		"${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}-dev.deb"
)

# vos-dev component: public headers + link libraries + pkg-config

# Install public headers under /usr/include/vos/ so external builds
# use the same include patterns as in-tree builds.
set(_vos_dev_includedir "include/vos")

install(DIRECTORY headers/
	DESTINATION ${_vos_dev_includedir}
	COMPONENT dev
	USE_SOURCE_PERMISSIONS
	PATTERN "private" EXCLUDE
	# agg, linprog: internal libs, not public API.
	PATTERN "libs" EXCLUDE
	PATTERN "tools" EXCLUDE
)

set(_vos_dev_libdir "lib/${VITRUVIAN_MULTIARCH_TRIPLE}")

# NAMELINK_SKIP: install the real .so, not the SONAME symlinks.
set(_vos_dev_libs be root game media2 opengl textencoding tracker translation)
foreach(_lib IN LISTS _vos_dev_libs)
	install(TARGETS ${_lib}
		LIBRARY DESTINATION ${_vos_dev_libdir}
		COMPONENT dev
		NAMELINK_SKIP
	)
endforeach()

# Compute VOS_CFLAGS from PUBLIC_HEADERS in build/headers.cmake.
set(_vos_cflags "")
foreach(_hdir IN LISTS PUBLIC_HEADERS)
	string(REGEX REPLACE "/$" "" _hdir_clean "${_hdir}")
	if(_hdir_clean STREQUAL "headers")
		list(APPEND _vos_cflags "-I/usr/${_vos_dev_includedir}")
	else()
		string(REGEX REPLACE "^headers/" "" _rel "${_hdir_clean}")
		list(APPEND _vos_cflags "-I/usr/${_vos_dev_includedir}/${_rel}")
	endif()
endforeach()
list(REMOVE_DUPLICATES _vos_cflags)

# In-tree builds get LinuxBuildCompatibility.h (status_t, int32, ...) via
# -include; external builds need it from pkg-config Cflags instead.
list(APPEND _vos_cflags
	"-I/usr/${_vos_dev_includedir}/build"
	"-I/usr/${_vos_dev_includedir}/build/config_headers"
	"-I/usr/${_vos_dev_includedir}/config"
	"-include LinuxBuildCompatibility.h"
)
string(REPLACE ";" " " VOS_PC_CFLAGS "${_vos_cflags}")

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/vos.pc.in"
	"${CMAKE_CURRENT_BINARY_DIR}/vos.pc"
	@ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vos.pc"
	DESTINATION "${_vos_dev_libdir}/pkgconfig"
	COMPONENT dev
)

INCLUDE(CPack)
