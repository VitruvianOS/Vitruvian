Application(ActivityMonitor
	SOURCES
	ActivityMonitor.cpp
	ActivityView.cpp
	ActivityWindow.cpp
	DataSource.cpp
	SettingsWindow.cpp
	SystemInfo.cpp
	SystemInfoHandler.cpp
	LIBS tracker shared localestub
	RDEF ActivityMonitor.rdef
)
UsePrivateHeaders(ActivityMonitor interface shared system)
