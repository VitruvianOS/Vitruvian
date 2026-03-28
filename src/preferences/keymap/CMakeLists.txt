Application(Keymap
	SOURCES
	KeyboardLayout.cpp
	KeyboardLayoutView.cpp
	Keymap.cpp
	KeymapApplication.cpp
	KeymapListItem.cpp
	KeymapWindow.cpp
	ModifierKeysWindow.cpp
	StatusMenuField.cpp
	LIBS tracker shared localestub
	RDEF Keymap.rdef
)
UsePrivateHeaders(Keymap shared interface)
