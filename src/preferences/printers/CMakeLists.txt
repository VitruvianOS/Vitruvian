Application(Printers
	SOURCES
		AddPrinterDialog.cpp
		PrintersApp.cpp
		PrintersWindow.cpp
		PrinterOptionsDialog.cpp
		PrinterWorker.cpp
	LIBS printcups ${CUPS_LIBRARIES} ${CAIRO_LIBRARIES} localestub
	RDEF Printers.rdef
)
UsePrivateHeaders(Printers print interface support)
DoCatalogs("x-vnd.Be-PRNT" preferences/printers)
