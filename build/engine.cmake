 #  Copyright 2019-2023, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

include(CMakeParseArguments)

include(build/defs.cmake)
include(build/deps.cmake)
include(build/headers.cmake)

# TODO: Add macros for Catalog
# TODO: Implement EnableWError( target )
# TODO: Add possibility to set compiler defs for a target
# TODO: Document macros

function( CompileRdef target rdef_file )
	cmake_parse_arguments(_ARG "STAGING" "" "" ${ARGN})

	set(_src    "${CMAKE_CURRENT_SOURCE_DIR}/${rdef_file}")
	set(_rsrc   "${CMAKE_CURRENT_BINARY_DIR}/${rdef_file}.rsrc")
	set(_bin    "$<TARGET_FILE:${target}>")
	set(_rc     "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/rc/rc")
	set(_xres   "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/xres")
	set(_rsattr "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/resattr")

	if(NOT EXISTS "${_src}")
		message(FATAL_ERROR "${_src} not found")
	endif()

	add_custom_command(TARGET ${target} POST_BUILD
		COMMENT "Building resource file ${rdef_file}"
		COMMAND "${_rc}" "${_src}" -o "${_rsrc}"
		COMMAND "${_xres}"   -o "${_bin}" "${_rsrc}"
		COMMAND "${_rsattr}" -O -o "${_bin}" "${_rsrc}"
	)

	if(_ARG_STAGING)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/apps_attrs_staging"
			COMMAND ${CMAKE_COMMAND} -E touch           "${CMAKE_BINARY_DIR}/apps_attrs_staging/${target}"
			COMMAND "${_rsattr}" -O -o "${CMAKE_BINARY_DIR}/apps_attrs_staging/${target}" "${_rsrc}"
		)
	endif()

	set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_rsrc}")
endfunction()

# Usage:
# Application(
# 	{targetname}
#	LIBS
#	{libslist}
#	INCLUDES
#	{includesList}
# )

macro( Application name )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES RDEF )

	cmake_parse_arguments( _APPLICATION "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	if( _APPLICATION_SOURCES )
		add_executable(${name} ${_APPLICATION_SOURCES})
	else()
		message( FATAL_ERROR "APPLICATION: 'SOURCES' argument required." )
	endif()

	list (INSERT _APPLICATION_LIBS 0 be)
	list (INSERT _APPLICATION_LIBS 0 root)
	target_link_libraries(${name} PUBLIC ${_APPLICATION_LIBS})

	# Add current dir headers
	list (APPEND _APPLICATION_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(${name} PRIVATE ${_APPLICATION_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")

	foreach( RDEF_FILE ${_APPLICATION_RDEF} )
		CompileRdef(${name} ${RDEF_FILE} STAGING)
	endforeach()
endmacro()

macro( Server name )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES RDEF )

	cmake_parse_arguments( _SERVER "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	add_executable(${name} ${_SERVER_SOURCES})

	list (INSERT _SERVER_LIBS 0 be)
	list (INSERT _SERVER_LIBS 0 root)
	target_link_libraries(${name} PUBLIC ${_SERVER_LIBS})

	list (APPEND _SERVER_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(${name} PRIVATE ${_SERVER_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")

	foreach( RDEF_FILE ${_SERVER_RDEF} )
		CompileRdef(${name} ${RDEF_FILE} STAGING)
	endforeach()
endmacro()

macro( AddOn name type )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES RDEF )

	cmake_parse_arguments( _ADDON "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	add_library(${name} ${type} ${_ADDON_SOURCES})

	target_link_libraries(${name} PRIVATE ${_ADDON_LIBS})

	# Add current dir headers
	list ( APPEND _ADDON_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} )
	target_include_directories(${name} PRIVATE ${_ADDON_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")

	foreach( RDEF_FILE ${_ADDON_RDEF} )
		CompileRdef(${name} ${RDEF_FILE} STAGING)
	endforeach()
endmacro()

macro( Test name )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES RDEF )

	cmake_parse_arguments( _TEST "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	# TODO support for resources (rdef)

	if( _TEST_SOURCES )
		if (NOT "${name}" STREQUAL "GLOBAL")
			add_executable(${name} ${_TEST_SOURCES})
		endif()
	else()
		message( FATAL_ERROR "TEST: 'SOURCES' argument required." )
	endif()

	list (INSERT _TEST_LIBS 0 be)
	list (INSERT _TEST_LIBS 0 root)
	target_link_libraries(${name} PUBLIC ${_TEST_LIBS})

	# Add current dir headers
	list (APPEND _TEST_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(${name} PRIVATE ${_TEST_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")

	foreach( RDEF_FILE ${_TEST_RDEF} )
		CompileRdef(${name} ${RDEF_FILE})
	endforeach()
endmacro()

function( UsePrivateHeaders target )
	set(_private_headers
		app
		add-ons/input_server/
		binary_compatibility
		graphics
		graphics/common
		graphics/vesa
		input
		interface
		kernel
		libroot
		libroot2
		locale
		mount
		notification
		print
		runtime_loader
		shared
		storage
		storage/mime
		storage/sniffer
		support
		syslog_daemon
		system
		textencoding
		tracker
		net
		screen_saver
	)

	foreach(arg IN LISTS ARGN)
		if ("${arg}" IN_LIST _private_headers)
			if("${target}" STREQUAL "GLOBAL")
				include_directories("${PROJECT_SOURCE_DIR}/headers/private/${arg}/")
			else()
				target_include_directories(
					${target}
					PRIVATE
					"${PROJECT_SOURCE_DIR}/headers/private/${arg}/"
				)
			endif()
			#message(STATUS "\n ${target} ${arg} \n")
		endif()
	endforeach()
	if(NOT "${target}" STREQUAL "GLOBAL")
		target_include_directories(
			${target}
			PRIVATE
			"${PROJECT_SOURCE_DIR}/headers/private/"
		)
	endif()
endfunction()

# TODO we can probably merge the buildtools-mode functions with the normal
# ones.
function( UsePrivateBuildHeaders target )
	set(_private_headers
		app
		add-ons/input_server/
		binary_compatibility
		graphics
		graphics/common
		graphics/vesa
		input
		interface
		kernel
		libroot
		libroot2
		locale
		mount
		notification
		print
		runtime_loader
		shared
		storage
		storage/mime
		storage/sniffer
		support
		syslog_daemon
		system
		textencoding
		tracker
		net
		screen_saver
	)

	foreach(arg IN LISTS ARGN)
		if ("${arg}" IN_LIST _private_headers)
			if("${target}" STREQUAL "GLOBAL")
				include_directories("${PROJECT_SOURCE_DIR}/headers/build/private/${arg}/")
			else()
				target_include_directories(
					${target}
					PRIVATE
					"${PROJECT_SOURCE_DIR}/headers/build/private/${arg}/"
				)
			endif()
			#message(STATUS "\n ${target} ${arg} \n")
		endif()
	endforeach()
	if(NOT "${target}" STREQUAL "GLOBAL")
		target_include_directories(
			${target}
			PRIVATE
			"${PROJECT_SOURCE_DIR}/headers/build/private/"
		)
	endif()
endfunction()

macro( BuildModeApplication name )
	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES RDEF )

	cmake_parse_arguments( _APPLICATION "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	if( _APPLICATION_SOURCES )
		add_executable(${name} ${_APPLICATION_SOURCES})
	else()
		message( FATAL_ERROR "APPLICATION: 'SOURCES' argument required." )
	endif()

	list (INSERT _APPLICATION_LIBS 0 be_build)
	list (INSERT _APPLICATION_LIBS 0 root_build)
	target_link_libraries(${name} PUBLIC ${_APPLICATION_LIBS})

	# Add current dir headers
	list (APPEND _APPLICATION_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(${name} PRIVATE ${_APPLICATION_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")

	# I suppose RDEFs are not needed here, but in case feel free to add it.
endmacro()
