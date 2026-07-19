include(${CMAKE_CURRENT_LIST_DIR}/apps.cmake)


set(SYSTEM_SERVERS
	janus
	janus_launch
	app_server
	input_server
	notification_server
	mount_server
	registrar
)
ImageInclude("/system/servers" ${SYSTEM_SERVERS})


set(INPUT_SERVER_ADDONS
	keyboard
	mouse
)
ImageInclude("/system/add-ons/input_server/devices" ${INPUT_SERVER_ADDONS})


#set(INPUT_SERVER_FILTERS
#    shortcut_catcher
#    switch_workspace
#    minimize_all
#)
#ImageInclude("/system/add-ons/input_server/filters" ${INPUT_SERVER_FILTERS})


set(OPENGL_RENDERERS
	mesa_surfaceless
)
ImageInclude("/system/add-ons/opengl" ${OPENGL_RENDERERS})


#set(INPUT_SERVER_FILTERS
#	shortcut_catcher
#	switch_workspace
#	minimize_all
#)
#ImageInclude("/system/add-ons/input_server/filters" ${INPUT_SERVER_FILTERS})


set(SYSTEM_LIBS
	root
	be
	game
	media2
	opengl
	textencoding
	tracker
	translation
	textencoding
	shared
	localestub
)
ImageInclude("/lib" ${SYSTEM_LIBS})


set(SYSTEM_TRANSLATORS
	BMPTranslator
	GIFTranslator
	HVIFTranslator
	ICNSTranslator
	ICOTranslator
	JPEGTranslator
	PCXTranslator
	PNGTranslator
	PPMTranslator
	PSDTranslator
	RAWTranslator
	RTFTranslator
	SGITranslator
	STXTTranslator
	TGATranslator
	TIFFTranslator
	WEBPTranslator
	WonderbrushTranslator
)
ImageInclude("/system/add-ons/Translators" ${SYSTEM_TRANSLATORS})


set(TRACKER_ADDONS
	FileType
	IconVader
	OpenTargetFolder
	OpenTerminal
	ZipOMatic
#	mark_as: needs libmail (not ported); skipped
)
ImageInclude("/system/add-ons/Tracker" ${TRACKER_ADDONS})


include(${CMAKE_CURRENT_LIST_DIR}/preferences.cmake)


include(${CMAKE_CURRENT_LIST_DIR}/bin.cmake)


# App attrs tarball: each Application/Server/AddOn macro creates a zero-byte
# placeholder at apps_attrs_staging/${name} with BeOS xattrs written by
# resattr -O. Here we arrange them into install paths and tar with --xattrs.
# postinst extracts to a temp dir and applies attrs to the real installed binaries.
set(_FLAT   "${CMAKE_BINARY_DIR}/apps_attrs_staging")
set(_FINAL  "${CMAKE_BINARY_DIR}/apps_attrs_final")
set(_TAR    "${CMAKE_BINARY_DIR}/apps_attrs.tar")

set(_ARRANGE_CMDS
    COMMAND ${CMAKE_COMMAND} -E rm -rf   "${_FINAL}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/apps"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/servers"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/preferences"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/add-ons/Tracker"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/add-ons/Translators"
)

foreach(_app ${SYSTEM_APPS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/apps/${_app}"
    )
endforeach()

foreach(_app ${DESKBAR_DEMOS_TARGETS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/apps/${_app}"
    )
endforeach()

foreach(_app ${DESKBAR_APPLETS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/apps/${_app}"
    )
endforeach()

foreach(_app ${CORE_APPLICATIONS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/${_app}"
    )
endforeach()

foreach(_server ${SYSTEM_SERVERS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_server}" "${_FINAL}/system/servers/${_server}"
    )
endforeach()

# SYSTEM_PREFERENCES_TARGETS and SYSTEM_PREFERENCES are parallel lists:
# targets have cmake-unique names; SYSTEM_PREFERENCES has the installed output names.
# (Deskbar_prefs/Tracker_prefs clash with the app targets of the same name in Jam,
# which uses per-subdirectory scoping — cmake uses a single global target namespace.)
list(LENGTH SYSTEM_PREFERENCES_TARGETS _nprefs)
math(EXPR _nprefs_last "${_nprefs} - 1")
foreach(_i RANGE ${_nprefs_last})
    list(GET SYSTEM_PREFERENCES_TARGETS ${_i} _target)
    list(GET SYSTEM_PREFERENCES         ${_i} _outname)
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_target}" "${_FINAL}/system/preferences/${_outname}"
    )
endforeach()

foreach(_addon ${TRACKER_ADDONS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_addon}" "${_FINAL}/system/add-ons/Tracker/${_addon}"
    )
endforeach()

foreach(_translator ${SYSTEM_TRANSLATORS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_translator}" "${_FINAL}/system/add-ons/Translators/${_translator}"
    )
endforeach()

list(APPEND _ARRANGE_CMDS
    COMMAND tar --xattrs -cf "${_TAR}" -C "${_FINAL}" .
)

add_custom_target(apps_attrs ALL
    ${_ARRANGE_CMDS}
    DEPENDS ${SYSTEM_APPS} ${DESKBAR_DEMOS_TARGETS} ${DESKBAR_APPLETS} ${CORE_APPLICATIONS} ${SYSTEM_SERVERS} ${SYSTEM_PREFERENCES_TARGETS} ${TRACKER_ADDONS} ${SYSTEM_TRANSLATORS}
    COMMENT "Packaging app attrs"
)

install(FILES "${_TAR}" DESTINATION /usr/share/vos)


# Tracker "New" templates need BEOS:TYPE xattrs so Tracker's New submenu
# can classify them and pick the right MIME icon. Source files in the tree
# carry no xattrs (git doesn't preserve them); stage + tar with --xattrs.
set(_TPL_SRC    "${CMAKE_SOURCE_DIR}/src/data/settings/tracker_new_templates")
set(_TPL_STAGE  "${CMAKE_BINARY_DIR}/templates_staging")
set(_TPL_TAR    "${CMAKE_BINARY_DIR}/templates_attrs.tar")

# Template -> BEOS:TYPE mapping. SMIM prefix is the B_MIME_STRING_TYPE
# type code (see Haiku's TypeConstants.h).
set(_TPL_NAMES        "C++ header" "C++ source" "Makefile" "Person"          "text file")
set(_TPL_MIME_TYPES   "text/x-source-code"
                      "text/x-source-code"
                      "text/x-makefile"
                      "application/x-person"
                      "text/plain")

set(_TPL_CMDS
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_TPL_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_TPL_STAGE}"
)
list(LENGTH _TPL_NAMES _ntpl)
math(EXPR _ntpl_last "${_ntpl} - 1")
foreach(_i RANGE ${_ntpl_last})
    list(GET _TPL_NAMES      ${_i} _name)
    list(GET _TPL_MIME_TYPES ${_i} _mime)
    list(APPEND _TPL_CMDS
        COMMAND ${CMAKE_COMMAND} -E copy "${_TPL_SRC}/${_name}" "${_TPL_STAGE}/${_name}"
        COMMAND setfattr -n user.beos.BEOS:TYPE -v "SMIM${_mime}" "${_TPL_STAGE}/${_name}"
    )
endforeach()
list(APPEND _TPL_CMDS
    COMMAND tar --xattrs -cf "${_TPL_TAR}" -C "${_TPL_STAGE}" .
)

add_custom_target(templates_attrs ALL
    ${_TPL_CMDS}
    COMMENT "Packaging Tracker template attrs"
)

install(FILES "${_TPL_TAR}" DESTINATION /usr/share/vos)
