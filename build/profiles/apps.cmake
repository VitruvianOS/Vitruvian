set(CORE_APPLICATIONS
	Deskbar
	Tracker
)
ImageInclude("/system/" ${CORE_APPLICATIONS})

# Greeter binary — /system/servers/ so janus finds it by name.
ImageInclude("/system/servers" vitruvian-login)

# Polkit authentication agent (auto-started with the graphical session).
ImageInclude("/system/servers" vos-polkit-agent)

set(SYSTEM_APPS
	AboutSystem
	ActivityMonitor
	DeskCalc
	DiskProbe
	DiskUsage
	DriveSetup
	Expander
	GLTeapot
	Installer
	LaunchBox
	Magnify
	PackageManager
	People
	ResEdit
	#Screenshot
	ShowImage
	StyledEdit
	Terminal
	#TextSearch
	Workspaces
)
ImageInclude("/system/apps" ${SYSTEM_APPS})

# Installed but not in the Deskbar menu: started by the system, not users.
set(SYSTEM_APPS_UNLISTED
	FirstBootPrompt
)
ImageInclude("/system/apps" ${SYSTEM_APPS_UNLISTED})

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Applications\")
")
foreach(app ${SYSTEM_APPS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Applications/${app}\"
			SYMBOLIC)
	")
endforeach()

set(DESKBAR_DEMOS
	#FontDemo
	Gradients
	Mandelbrot
	Pairs
	PrivilegedGuy
	Sudoku
)

set(DESKBAR_DEMOS_TARGETS
	Gradients
	Mandelbrot
	Pairs
	PrivilegedGuy
	Sudoku
)

ImageInclude("/system/apps" ${DESKBAR_DEMOS_TARGETS})

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Demos\")
")
foreach(app ${DESKBAR_DEMOS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Demos/${app}\"
			SYMBOLIC)
	")
endforeach()

set(DESKBAR_APPLETS
	AudioMixer
	#AutoRaise
	BluetoothStatus
	Clock
	NetworkStatus
	OverlayImage
	PowerStatus
	ProcessController
	Pulse
)

ImageInclude("/system/apps" ${DESKBAR_APPLETS})

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Desktop applets\")
")
foreach(app ${DESKBAR_APPLETS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Desktop applets/${app}\"
			SYMBOLIC)
	")
endforeach()
