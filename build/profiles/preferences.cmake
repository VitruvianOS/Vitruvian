# Target names (cmake-unique): used for ImageInclude and DEPENDS
set(SYSTEM_PREFERENCES_TARGETS
	Accounts
	Appearance
	Backgrounds
	Bluetooth
	Camera_prefs
	DataTranslations
	Deskbar_prefs
	FileTypes
	Hardware
	Input
	Joysticks
	Keymap
	Locale
	Location
	Media
	Network
	Power
	Printers
	Screen
	ScreenSaver
	Shortcuts
	Time
	Tracker_prefs
	User
)

# Installed binary names (OUTPUT_NAME where it differs): used for symlinks and staging
set(SYSTEM_PREFERENCES
	Accounts
	Appearance
	Backgrounds
	Bluetooth
	Camera
	DataTranslations
	Deskbar
	FileTypes
	Hardware
	Input
	Joysticks
	Keymap
	Locale
	Location
	Media
	Network
	Power
	Printers
	Screen
	ScreenSaver
	Shortcuts
	Time
	Tracker
	User
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
