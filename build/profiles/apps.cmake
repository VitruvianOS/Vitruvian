set(CORE_APPLICATIONS
	Deskbar
	Tracker
)
ImageInclude("/system/" ${CORE_APPLICATIONS})

set(SYSTEM_APPS
	AboutSystem
	ActivityMonitor
	AutoRaise
	Clock
	DeskCalc
	DiskProbe
	DiskUsage
	DriveSetup
	Expander
	FirstBootPrompt
	FontDemo
	Gradients
	Installer
	LaunchBox
	Magnify
	Mandelbrot
	OverlayImage
	Pairs
	People
	PowerStatus
	ProcessController
	Pulse
	ResEdit
	Screenshot
	ShowImage
	StyledEdit
	Sudoku
	Switcher
	Terminal
	TextSearch
	Workspaces
)
ImageInclude("/system/apps" ${SYSTEM_APPS})

set(DESKBAR_APPLICATIONS
	AboutSystem
	ActivityMonitor
	DeskCalc
	DiskProbe
	DiskUsage
	DriveSetup
	Expander
	Installer
	LaunchBox
	Magnify
	People
	ProcessController
	ResEdit
	Screenshot
	ShowImage
	StyledEdit
	Switcher
	Terminal
	TextSearch
	Workspaces
)

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

set(DESKBAR_DEMOS
	FontDemo
	Gradients
	Mandelbrot
	Pairs
	Sudoku
)

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
	AutoRaise
	Clock
	OverlayImage
	PowerStatus
	Pulse
)

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
