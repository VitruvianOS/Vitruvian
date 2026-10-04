Application(Printers
	SOURCES
		AddPrinterDialog.cpp
		PrintersApp.cpp
		PrintersWindow.cpp
		PrinterOptionsDialog.cpp
		PrinterWorker.cpp
	LIBS printcups libscan ${CUPS_LIBRARIES} ${CAIRO_LIBRARIES} ${SANE_LIBRARIES} localestub
	RDEF Printers.rdef
)
UsePrivateHeaders(Printers print scan interface support)
DoCatalogs("x-vnd.Be-PRNT" preferences/printers)
