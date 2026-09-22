 #  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

function( ImageInclude path )
	foreach(arg IN LISTS ARGN)
		install(TARGETS ${arg}
			COMPONENT ${path}
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
	install(FILES ${source} DESTINATION ${dest})
endfunction()

function( ImageIncludeDir source dest )
	# Preserve +x on shipped scripts (e.g. /system/boot/first_login/*).
	install(DIRECTORY ${source} DESTINATION ${dest} USE_SOURCE_PERMISSIONS)
endfunction()

function( ImageCreateDir dest )
	install(DIRECTORY DESTINATION ${dest})
endfunction()

include(build/baseimage.cmake)
include(build/profiles/base.cmake)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
include(build/profiles/debug.cmake)
endif()

include(build/profiles/data.cmake)

string(REPLACE ";" "," RESULT "${RUN_LIST}")

set(CORE_DEPS "${RESULT}")

set(CPACK_DEBIAN_PACKAGE_DEPENDS ${CORE_DEPS})
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${VITRUVIAN_TARGET_ARCH})
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
	"${CMAKE_CURRENT_SOURCE_DIR}/data/debian/postinst"
	"${CMAKE_CURRENT_SOURCE_DIR}/data/debian/prerm"
	"${CMAKE_CURRENT_SOURCE_DIR}/data/debian/postrm")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Vitruvian Project")

execute_process(
	COMMAND git rev-parse --short HEAD
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE VOS_GIT_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(VOS_GIT_SHA)
	set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}+git${VOS_GIT_SHA}")
	message(STATUS "VOS package version: ${CPACK_DEBIAN_PACKAGE_VERSION}")
else()
	message(WARNING "no git sha available - package version stays ${PROJECT_VERSION}, which collides in the pool on the next rebuild")
endif()
INCLUDE(CPack)

# Make `ninja clean` (and `make clean`) wipe CPack outputs too. CPack writes
# its artifacts into the build root, so they normally survive `clean` and
# accumulate stale .deb files across reconfigurations.
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
	ADDITIONAL_CLEAN_FILES
		"${CMAKE_BINARY_DIR}/_CPack_Packages"
		"${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.deb"
)
