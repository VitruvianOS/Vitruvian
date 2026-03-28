# Target names (cmake-unique) — used for ImageInclude and DEPENDS
set(SYSTEM_PREFERENCES_TARGETS
	Appearance
	Backgrounds
	DataTranslations
	Deskbar_prefs
	FileTypes
	Input
	Keymap
	Locale
	ScreenSaver
	Shortcuts
	Tracker_prefs
)

# Installed binary names (OUTPUT_NAME where it differs) — used for symlinks and staging
set(SYSTEM_PREFERENCES
	Appearance
	Backgrounds
	DataTranslations
	Deskbar
	FileTypes
	Input
	Keymap
	Locale
	ScreenSaver
	Shortcuts
	Tracker
)

ImageInclude("/system/preferences" ${SYSTEM_PREFERENCES_TARGETS})

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Preferences\")
")
foreach(pref ${SYSTEM_PREFERENCES})
	install(CODE "
		file(CREATE_LINK \"/system/preferences/${pref}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Preferences/${pref}\"
			SYMBOLIC)
	")
endforeach()
