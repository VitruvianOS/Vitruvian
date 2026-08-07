 #  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

include(CMakeParseArguments)

include(build/defs.cmake)
include(build/deps.cmake)
include(build/headers.cmake)

# Program interpreter path for RunnableAddOn (a .so that is also execve-able).
if(NOT DEFINED VOS_DYNAMIC_LINKER)
	execute_process(
		COMMAND ${CMAKE_C_COMPILER} -dumpmachine
		OUTPUT_VARIABLE _vos_triple OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(_vos_triple STREQUAL "x86_64-linux-gnu")
		set(VOS_DYNAMIC_LINKER "/lib64/ld-linux-x86-64.so.2")
	elseif(_vos_triple STREQUAL "aarch64-linux-gnu")
		set(VOS_DYNAMIC_LINKER "/lib/ld-linux-aarch64.so.1")
	elseif(_vos_triple STREQUAL "riscv64-linux-gnu")
		set(VOS_DYNAMIC_LINKER "/lib/ld-linux-riscv64-lp64d.so.1")
	else()
		message(FATAL_ERROR "RunnableAddOn: unknown interpreter for ${_vos_triple}")
	endif()
endif()

# An explicit .interp section makes ld emit PT_INTERP for a -shared link (which
# -Wl,--dynamic-linker does not); same trick glibc uses to make libc.so.6 runnable.
set(VOS_RUNNABLE_INTERP_S "${CMAKE_BINARY_DIR}/runnable_addon_interp.S")
file(WRITE "${VOS_RUNNABLE_INTERP_S}"
	".section .interp,\"a\"\n.asciz \"${VOS_DYNAMIC_LINKER}\"\n")
# Pre-assemble to an object (the project enables only C/CXX, not ASM).
set(VOS_RUNNABLE_INTERP_O "${CMAKE_BINARY_DIR}/runnable_addon_interp.o")
execute_process(
	COMMAND ${CMAKE_C_COMPILER} -c "${VOS_RUNNABLE_INTERP_S}"
		-o "${VOS_RUNNABLE_INTERP_O}"
	RESULT_VARIABLE _vos_interp_rc)
if(NOT _vos_interp_rc EQUAL 0)
	message(FATAL_ERROR "RunnableAddOn: failed to assemble .interp object")
endif()

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
	if( BUILDTOOLS_DIR )
		set( _linkcatkeys "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/tools/locale/linkcatkeys" )
	else()
		# Self-hosting on Vitruvian: use the linkcatkeys installed system-wide.
		set( _linkcatkeys "linkcatkeys" )
	endif()

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
		DESTINATION "/system/data/locale/catalogs/${signature}"
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
	if(BUILDTOOLS_DIR)
		set(_rc "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/rc/rc")
	else()
		set(_rc "rc")
	endif()

	if(NOT EXISTS "${_src}")
		message(FATAL_ERROR "${_src} not found")
	endif()

	# Haiku's jam runs cpp on .rdef files before rc; mirror that so rdefs
	# can use #ifdef HAIKU_TARGET_PLATFORM_HAIKU etc.
	# TODO maybe we should clean that
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
	if(BUILDTOOLS_DIR)
		set(_xres   "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/xres")
		set(_rsattr "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/resattr")
	else()
		set(_xres   "xres")
		set(_rsattr "resattr")
	endif()

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
# TODO: translators under src/add-ons/translators/ build as SHARED add-ons only
# and lose their config GUI. Flip them to RunnableAddOn to restore dual-mode.

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

	# -fvisibility-inlines-hidden: keep inline/template instantiations add-on-local
	# so they don't collide with same-named symbols exported by libbe.so.
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

# A .so that is dlopen-able as an add-on and ALSO execve-able (PT_INTERP + _start
# -> main()).
macro( RunnableAddOn name )
	AddOn(${name} SHARED ${ARGN})

	# Auto-link be+root like Application (plain AddOn does not); --no-undefined
	# turns any missing symbol into a link error instead of a runtime crash.
	target_link_libraries(${name} PUBLIC be root)
	target_link_options(${name} PRIVATE "-Wl,--no-undefined")

	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")

	target_link_options(${name} PRIVATE
		"-Wl,-e,_start"                                # entry -> CRT _start
		"-nostartfiles"                                # we add Scrt1.o ourselves
		)
	# Stock PIE CRT: Scrt1.o provides _start -> __libc_start_main(main,...);
	# crti/crtn re-added because -nostartfiles dropped them.
	execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=Scrt1.o
		OUTPUT_VARIABLE _vos_scrt1 OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crti.o
		OUTPUT_VARIABLE _vos_crti OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtn.o
		OUTPUT_VARIABLE _vos_crtn OUTPUT_STRIP_TRAILING_WHITESPACE)
	# crtbeginS.o/crtendS.o (the -shared CRT bookends, also dropped by
	# -nostartfiles) define __dso_handle, needed by static-local dtors.
	execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtbeginS.o
		OUTPUT_VARIABLE _vos_crtbeginS OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtendS.o
		OUTPUT_VARIABLE _vos_crtendS OUTPUT_STRIP_TRAILING_WHITESPACE)
	target_link_options(${name} PRIVATE
		"${VOS_RUNNABLE_INTERP_O}"                     # .interp -> PT_INTERP
		"${_vos_crti}" "${_vos_scrt1}" "${_vos_crtbeginS}"
		"${_vos_crtendS}" "${_vos_crtn}")

	# double-clickable / execve-able on disk
	set_target_properties(${name} PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
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
