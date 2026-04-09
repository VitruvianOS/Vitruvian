set(CORE_APPLICATIONS
	Deskbar
	Tracker
)
ImageInclude("/system/" ${CORE_APPLICATIONS})

set(SYSTEM_APPS
	AboutSystem
	ActivityMonitor
	DeskCalc
	DiskProbe
	DiskUsage
	#DriveSetup
	Expander
	FirstBootPrompt
	Installer
	LaunchBox
	Magnify
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
	Sudoku
)

set(DESKBAR_DEMOS_TARGETS
	Gradients
	Mandelbrot
	Pairs
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
	#AutoRaise
	Clock
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
