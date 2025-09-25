 #  Copyright 2019-2023, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

include(CMakeParseArguments)

include(build/defs.cmake)
include(build/deps.cmake)
include(build/headers.cmake)

# TODO: There's some amount of code duplication here...
# TODO: Add macros for Catalog
# TODO: Implement EnableWError( target )
# TODO: Add possibility to set compiler defs for a target
# TODO: Document macros

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

	# TODO: support multiple rdefs
	if( _APPLICATION_RDEF )
		if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_APPLICATION_RDEF}")
			message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/${_APPLICATION_RDEF} not found\n")
			return()
		endif()
		message("${_APPLICATION_RDEF}")
		add_custom_command(TARGET ${name} POST_BUILD
			COMMAND echo "Compiling resource file for ${name}..."
			COMMAND "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/rc/rc" "${CMAKE_CURRENT_SOURCE_DIR}/${_APPLICATION_RDEF}" -o "${CMAKE_CURRENT_BINARY_DIR}/${name}"
			COMMAND echo "Adding resources to ${name} binary..."
			COMMAND "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/xres" -o "${CMAKE_CURRENT_BINARY_DIR}/${name}" "${CMAKE_CURRENT_BINARY_DIR}/${name}.rsrc"
		)
		set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${CMAKE_CURRENT_BINARY_DIR}/${name}.rsrc")
	endif()
endmacro()

macro( Server name )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES )

	cmake_parse_arguments( _SERVER "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	add_executable(${name} ${_SERVER_SOURCES})

	list (INSERT _SERVER_LIBS 0 be)
	list (INSERT _SERVER_LIBS 0 root)
	target_link_libraries(${name} PUBLIC ${_SERVER_LIBS})

	list (APPEND _SERVER_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(${name} PRIVATE ${_SERVER_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
endmacro()

macro( AddOn name type )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES )

	cmake_parse_arguments( _ADDON "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN} )

	add_library(${name} ${type} ${_ADDON_SOURCES})

	target_link_libraries(${name} PRIVATE ${_ADDON_LIBS})

	# Add current dir headers
	list ( APPEND _ADDON_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} )
	target_include_directories(${name} PRIVATE ${_ADDON_INCLUDES})

	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
endmacro()

macro( Test name )

	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS SOURCES LIBS INCLUDES )

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

endmacro()
