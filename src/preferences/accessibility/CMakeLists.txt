Application(Accessibility
	SOURCES
	Accessibility.cpp
	AccessibilityView.cpp
	AccessibilityWindow.cpp
	LIBS be shared localestub media2 game
	RDEF Accessibility.rdef
)

UsePrivateHeaders(Accessibility app interface preferences)
DoCatalogs("x-vnd.vos-Accessibility" preferences/accessibility)
