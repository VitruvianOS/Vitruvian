Application(SerialConnect
	SOURCES
	CustomRateWindow.cpp
	FileSender.cpp
	SerialApp.cpp
	SerialWindow.cpp
	TermView.cpp
	XModem.cpp
	libvterm/src/encoding.c
	libvterm/src/input.c
	libvterm/src/parser.c
	libvterm/src/pen.c
	libvterm/src/screen.c
	libvterm/src/state.c
	libvterm/src/unicode.c
	libvterm/src/vterm.c
	LIBS game tracker localestub
	RDEF SerialConnect.rdef

	INCLUDES
	"${CMAKE_CURRENT_SOURCE_DIR}/libvterm/include"
)
UsePrivateHeaders(SerialConnect interface)
DoCatalogs("x-vnd.vos.SerialConnect" apps/serialconnect)
