Application(PrintOMatic
	SOURCES
		PrintOMaticApp.cpp
		PrintOMaticWindow.cpp
		ScanView.cpp
	LIBS be libscan printcups translation shared tracker localestub ${CUPS_LIBRARIES} ${CAIRO_LIBRARIES} ${SANE_LIBRARIES}
	INCLUDES ${CMAKE_SOURCE_DIR}/src/libs/printcups
	RDEF PrintOMatic.rdef
)
UsePrivateHeaders(PrintOMatic print scan interface support translation)
DoCatalogs("x-vnd.vos-PrintOMatic" apps/printomatic)
