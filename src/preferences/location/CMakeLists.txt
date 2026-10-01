Application(Location
	SOURCES
	Location.cpp
	LocationProvider.cpp
	LocationSettings.cpp
	LocationSettingsView.cpp
	LocationWindow.cpp
	LIBS be shared localestub
	RDEF Location.rdef
)

DoCatalogs("x-vnd.vos-Location" preferences/location)
