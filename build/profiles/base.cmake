set(DESKBAR_APPLICATIONS
	StyledEdit
	Terminal
	Sudoku
	Pairs
	Clock
	AboutSystem
	DeskCalc
)
ImageInclude("/system/apps" ${DESKBAR_APPLICATIONS})

# Create Deskbar Applications symlinks for each DESKBAR_APPLICATIONS entry.
# Mirrors haiku-latest HaikuBootstrap AddSymlinkToPackage for the Applications menu.
# Adding an app to DESKBAR_APPLICATIONS above automatically gives it a Deskbar entry.
# $ENV{DESTDIR} is prepended to the link location so CPack staging works correctly;
# the link target stays as a bare runtime path since it's resolved at runtime.
install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Applications\")
")
foreach(app ${DESKBAR_APPLICATIONS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Applications/${app}\"
			SYMBOLIC)
	")
endforeach()


set(CORE_APPLICATIONS
	Deskbar
	Tracker
)
ImageInclude("/system/" ${CORE_APPLICATIONS})


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


set(SYSTEM_PREFERENCES
)
ImageInclude("/system/preferences" ${SYSTEM_PREFERENCES})


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
)

foreach(_app ${DESKBAR_APPLICATIONS})
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

list(APPEND _ARRANGE_CMDS
    COMMAND tar --xattrs -cf "${_TAR}" -C "${_FINAL}" .
)

add_custom_target(apps_attrs ALL
    ${_ARRANGE_CMDS}
    DEPENDS ${DESKBAR_APPLICATIONS} ${CORE_APPLICATIONS} ${SYSTEM_SERVERS}
    COMMENT "Packaging app attrs"
)

install(FILES "${_TAR}" DESTINATION /usr/share/vos)
