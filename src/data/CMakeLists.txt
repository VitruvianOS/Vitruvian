if(BUILDTOOLS_MODE STREQUAL "1")
	return()
endif()

set(MIME_SRC ${CMAKE_CURRENT_SOURCE_DIR}/mime_db)
set(MIME_OUT ${CMAKE_BINARY_DIR}/mime_db)
set(MIME_RSRC ${CMAKE_BINARY_DIR}/mime_rsrc)

set(RC      "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/rc/rc")
set(RESATTR "${CMAKE_BINARY_DIR}/${BUILDTOOLS_DIR}/src/bin/resattr")

set(MIME_DB_OUTPUTS)

# Helper: compile all subtypes in a supertype directory.
# SUB_DONE_DEP is the stamp file the subtypes should depend on (empty string if none).
macro(mime_db_subtypes SUPER SUB_DONE_DEP)
	file(GLOB SUBTYPE_FILES "${MIME_SRC}/${SUPER}/*")
	foreach(SUB_SRC ${SUBTYPE_FILES})
		get_filename_component(SUB_NAME ${SUB_SRC} NAME)
		set(SUB_RSRC ${MIME_RSRC}/${SUPER}/${SUB_NAME}.rsrc)
		set(SUB_OUT  ${MIME_OUT}/${SUPER}/${SUB_NAME})

		add_custom_command(
			OUTPUT ${SUB_RSRC}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_RSRC}/${SUPER}
			COMMAND ${RC} -o ${SUB_RSRC} ${SUB_SRC}
			DEPENDS ${SUB_SRC} ${RC} ${RESATTR}
			COMMENT "Compiling ${SUPER}/${SUB_NAME}"
		)

		if("${SUB_DONE_DEP}" STREQUAL "")
			set(_sub_extra_dep)
		else()
			set(_sub_extra_dep ${SUB_DONE_DEP})
		endif()

		add_custom_command(
			OUTPUT ${SUB_OUT}
			COMMAND ${RESATTR} -O -o ${SUB_OUT} ${SUB_RSRC}
			DEPENDS ${SUB_RSRC} ${_sub_extra_dep}
			COMMENT "Installing ${SUPER}/${SUB_NAME}"
		)
		list(APPEND MIME_DB_OUTPUTS ${SUB_OUT})
	endforeach()
endmacro()

# Supertypes that have both a <name>.super rdef and a <name>/ directory
set(SUPERTYPES application audio image message multipart text video)

foreach(SUPER ${SUPERTYPES})
	set(SUPER_SRC  ${MIME_SRC}/${SUPER}.super)
	set(SUPER_RSRC ${MIME_RSRC}/${SUPER}.super.rsrc)
	set(SUPER_OUT  ${MIME_OUT}/${SUPER})
	set(SUPER_DONE ${SUPER_OUT}/.super_done)

	file(MAKE_DIRECTORY ${SUPER_OUT})

	add_custom_command(
		OUTPUT ${SUPER_RSRC}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_RSRC}
		COMMAND ${RC} -o ${SUPER_RSRC} ${SUPER_SRC}
		DEPENDS ${SUPER_SRC} ${RC} ${RESATTR}
		COMMENT "Compiling ${SUPER}.super"
	)

	add_custom_command(
		OUTPUT ${SUPER_DONE}
		COMMAND ${RESATTR} -O -o ${SUPER_OUT} ${SUPER_RSRC}
		COMMAND ${CMAKE_COMMAND} -E touch ${SUPER_DONE}
		DEPENDS ${SUPER_RSRC}
		COMMENT "Installing attrs for ${SUPER}/"
	)
	list(APPEND MIME_DB_OUTPUTS ${SUPER_DONE})

	mime_db_subtypes(${SUPER} ${SUPER_DONE})
endforeach()

# Supertypes that only have a directory (no .super file)
set(BARE_SUPERTYPES locale)

foreach(SUPER ${BARE_SUPERTYPES})
	file(MAKE_DIRECTORY ${MIME_OUT}/${SUPER})
	mime_db_subtypes(${SUPER} "")
endforeach()

add_custom_target(mime_db ALL DEPENDS ${MIME_DB_OUTPUTS})

# Bundle the mime_db directory (including xattrs) into a tarball for postinst.
# CPack's install(DIRECTORY) does not preserve extended attributes, so we use
# tar --xattrs to capture them. postinst extracts to /system/data/.
set(MIME_TAR ${CMAKE_BINARY_DIR}/mime_db.tar)
add_custom_command(
	OUTPUT ${MIME_TAR}
	COMMAND tar --xattrs -cf ${MIME_TAR} -C ${CMAKE_BINARY_DIR} mime_db
	DEPENDS mime_db
	COMMENT "Packaging mime_db with xattrs"
)
add_custom_target(mime_db_tar ALL DEPENDS ${MIME_TAR})

install(FILES ${MIME_TAR}
	DESTINATION /usr/share/vos)
