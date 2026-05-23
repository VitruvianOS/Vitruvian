AddOn(
	virtualkeyboard
	SHARED

	LIBS
	be shared tracker localestub

	SOURCES
	VirtualKeyboardInputDevice.cpp
	VirtualKeyboardWindow.cpp
	KeyboardLayoutView.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/keymap/KeyboardLayout.cpp
	${CMAKE_SOURCE_DIR}/src/preferences/keymap/Keymap.cpp

	INCLUDES
	"./"
	"../"
)

UsePrivateHeaders(virtualkeyboard input interface shared)

target_compile_options(virtualkeyboard PRIVATE "-iquote${CMAKE_SOURCE_DIR}/src/preferences/keymap")
