Application(Services
	SOURCES
	main.cpp
	ServicesApp.cpp
	ServicesList.cpp
	ServicesUnit.cpp
	ServicesWindow.cpp
	ServicesWorker.cpp
	LIBS localestub systemd
	RDEF Services.rdef
)
UsePrivateHeaders(Services app interface shared)
DoCatalogs("x-vnd.vos-Services" preferences/services)
