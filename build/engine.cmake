 #  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

include(CMakeParseArguments)

include(build/defs.cmake)
include(build/deps.cmake)
include(build/headers.cmake)

# TODO: Implement EnableWError( target )
# TODO: Add possibility to set compiler defs for a target
# TODO: Document macros

macro( DoCatalogs signature subdir )
	set( _catalogs_src "${CMAKE_SOURCE_DIR}/data/catalogs/${subdir}" )
	if( NOT EXISTS "${_catalogs_src}" )
		return()
	endif()

	file( GLOB _catkeys_files "${_catalogs_src}/*.catkeys" )
	list( LENGTH _catkeys_files _catkeys_count )
	if( _catkeys_count EQUAL 0 )
		return()
	endif()

	set( _catalog_dir "${CMAKE_BINARY_DIR}/catalogs/${signature}" )
	set( _linkcatkeys "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/tools/locale/linkcatkeys" )

	foreach( _catkeys ${_catkeys_files} )
		get_filename_component( _lang ${_catkeys} NAME_WE )
		set( _catalog_output "${_catalog_dir}/${_lang}.catalog" )

		add_custom_command(
			OUTPUT "${_catalog_output}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${_catalog_dir}"
			COMMAND "${_linkcatkeys}" -s "${signature}" -l "${_lang}" -tf -o "${_catalog_output}" "${_catkeys}"
			DEPENDS "${_linkcatkeys}" "${_catkeys}"
			COMMENT "Building catalog ${signature}/${_lang}"
		)

		list( APPEND _catalog_outputs "${_catalog_output}" )
	endforeach()

	add_custom_target( catalogs_${signature} ALL DEPENDS ${_catalog_outputs} )

	install( DIRECTORY "${_catalog_dir}/"
		DESTINATION /system/data/locale/catalogs
		FILES_MATCHING PATTERN "*.catalog"
	)
endmacro()

# Compile a single .rdef to a .rsrc file (POST_BUILD), and record the output
# path in the target property RDEF_RSRC_FILES so LinkRdefs can embed them all
# in one xres call.
function( CompileRdef target rdef_file )
	cmake_parse_arguments(_ARG "STAGING" "" "" ${ARGN})

	set(_src    "${CMAKE_CURRENT_SOURCE_DIR}/${rdef_file}")
	set(_pp     "${CMAKE_CURRENT_BINARY_DIR}/${rdef_file}.pp")
	set(_rsrc   "${CMAKE_CURRENT_BINARY_DIR}/${rdef_file}.rsrc")
	set(_rc     "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/rc/rc")

	if(NOT EXISTS "${_src}")
		message(FATAL_ERROR "${_src} not found")
	endif()

	# Haiku's jam runs cpp on .rdef files before rc; mirror that so rdefs
	# can use #ifdef HAIKU_TARGET_PLATFORM_HAIKU etc.
	add_custom_command(TARGET ${target} POST_BUILD
		COMMENT "Compiling rdef ${rdef_file}"
		COMMAND "${CMAKE_C_COMPILER}" -E -x c -DHAIKU_TARGET_PLATFORM_HAIKU
			-D__VOS__ -P "${_src}" -o "${_pp}"
		COMMAND "${_rc}" "${_pp}" -o "${_rsrc}"
	)

	# Accumulate rsrc paths and the staging flag on the target.
	set_property(TARGET ${target} APPEND PROPERTY RDEF_RSRC_FILES "${_rsrc}")
	if(_ARG_STAGING)
		set_property(TARGET ${target} PROPERTY RDEF_STAGING TRUE)
	endif()

	set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_rsrc}")
endfunction()

# Embed all accumulated .rsrc files into the binary in a single xres call.
# Must be called after all CompileRdef() calls for a given target.
function( LinkRdefs target )
	set(_bin    "$<TARGET_FILE:${target}>")
	set(_xres   "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/xres")
	set(_rsattr "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/resattr")

	get_property(_rsrc_files TARGET ${target} PROPERTY RDEF_RSRC_FILES)
	get_property(_staging    TARGET ${target} PROPERTY RDEF_STAGING)

	if(NOT _rsrc_files)
		return()
	endif()

	add_custom_command(TARGET ${target} POST_BUILD
		COMMENT "Embedding resources into ${target}"
		COMMAND "${_xres}" -o "${_bin}" ${_rsrc_files}
	)

	foreach(_rsrc IN LISTS _rsrc_files)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND "${_rsattr}" -O -o "${_bin}" "${_rsrc}"
		)
	endforeach()

	if(_staging)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/apps_attrs_staging"
			COMMAND ${CMAKE_COMMAND} -E touch           "${CMAKE_BINARY_DIR}/apps_attrs_staging/${target}"
		)
		foreach(_rsrc IN LISTS _rsrc_files)
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND "${_rsattr}" -O -o "${CMAKE_BINARY_DIR}/apps_attrs_staging/${target}" "${_rsrc}"
			)
		endforeach()
	endif()
endfunction()

# Usage:
# Application(
# 	{targetname}
#	LIBS
#	{libslist}
#	INCLUDES
#	{includesList}
# )
#
# TODO: BeOS/Haiku translators are dual-mode (double-click = config app,
# load_add_on = translator). Linux dlopen refuses PIE executables, so the
# translators under src/add-ons/translators/ currently build as SHARED
# add-ons only (see their CMakeLists). Restore dual-mode by teaching
# Application() to emit a dlopen-able binary (entry shim + -shared -Wl,-E),
# then flip those translators back to Application().

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
	LinkRdefs(${name})
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
	LinkRdefs(${name})
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

	# -fvisibility-inlines-hidden: keep inline/template method instantiations
	# private to this add-on so they cannot collide with same-named symbols
	# exported by libbe.so (e.g. global TReadHelper from MessageUtils.h vs
	# a translator-local helper of the same name). Regular non-inline
	# entry points (make_nth_translator, process_refs, ...) are unaffected.
	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h -fvisibility-inlines-hidden")

	foreach( RDEF_FILE ${_ADDON_RDEF} )
		CompileRdef(${name} ${RDEF_FILE} STAGING)
	endforeach()
	LinkRdefs(${name})
endmacro()

# Bare output name (no "lib" prefix, no ".so" suffix) — Tracker's
# add-on menu and BTranslatorRoster enumerate by directory walk and use
# the filename as the menu label.
macro( TrackerAddOn name )
	AddOn(${name} ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
endmacro()

macro( TranslatorAddOn name )
	AddOn(${name} ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
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
	LinkRdefs(${name})
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
		system
		textencoding
		tracker
		net
		screen_saver
		preferences
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
		system
		textencoding
		tracker
		net
		screen_saver
		preferences
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
