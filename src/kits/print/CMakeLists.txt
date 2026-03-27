AddOn(printutils
	STATIC

	SOURCES
	BeUtils.cpp
	BeUtilsTranslation.cpp
	FolderWatcher.cpp
	Jobs.cpp
	JobSetupPanel.cpp
	PictureIterator.cpp
	PicturePrinter.cpp
	PrintAddOnServer.cpp
	PrintAddOnServerProtocol.cpp
	Printer.cpp
	PrinterDriverAddOn.cpp
	PrinterRoster.cpp
	PrintPanel.cpp
	PrintTransport.cpp
	PrintTransportAddOn.cpp
	Template.cpp
)

UsePrivateHeaders(printutils print shared)
target_include_directories(printutils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
