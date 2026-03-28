include(${CMAKE_CURRENT_LIST_DIR}/apps.cmake)


set(SYSTEM_SERVERS
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


set(SYSTEM_LIBS
	root
	be
	game
	textencoding
	tracker
	translation
	textencoding
)
ImageInclude("/lib" ${SYSTEM_LIBS})


set(SYSTEM_TRANSLATORS
#	BMPTranslator
#	EXRTranslator
#	GIFTranslator
	HVIFTranslator
#	ICNSTranslator
#	ICOTranslator
#	JPEGTranslator
#	PNGTranslator
#	PPMTranslator
#	PSDTranslator
	RAWTranslator
#	RTFTranslator
#	SGITranslator
#	STXTTranslator
#	TGATranslator
#	TIFFTranslator
#	WEBPTranslator
#	WonderbrushTranslator
)
ImageInclude("/system/add-ons/Translators" ${SYSTEM_TRANSLATORS})


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
)

foreach(_app ${SYSTEM_APPS})
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

list(APPEND _ARRANGE_CMDS
    COMMAND tar --xattrs -cf "${_TAR}" -C "${_FINAL}" .
)

add_custom_target(apps_attrs ALL
    ${_ARRANGE_CMDS}
    DEPENDS ${SYSTEM_APPS} ${CORE_APPLICATIONS} ${SYSTEM_SERVERS} ${SYSTEM_PREFERENCES_TARGETS}
    COMMENT "Packaging app attrs"
)

install(FILES "${_TAR}" DESTINATION /usr/share/vos)
