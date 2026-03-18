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
	registrar
	mount_server
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


set(BIN_DIRECTORY
	addattr
	catarea
	catattr
	copyattr
	finddir
	hey
	listarea
	listattr
	listres
	lsindex
	mkindex
	mvattr
	query
	reindex
	rmattr
	rmindex
	rc
	setmime
	shutdown
	system_time
	translate
	watch
	xres
)
ImageInclude("/bin" ${BIN_DIRECTORY})
