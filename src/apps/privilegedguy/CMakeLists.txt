Application(PrivilegedGuy
	SOURCES
	MainWindow.cpp
	PrivilegedGuyApp.cpp
	LIBS shared localestub
	RDEF PrivilegedGuy.rdef
)
UsePrivateHeaders(PrivilegedGuy app interface shared)
DoCatalogs("application/x-vnd.Vitruvian-PrivilegedGuy" apps/privilegedguy)
