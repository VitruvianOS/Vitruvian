Application(
	StyledEdit

	SOURCES
	ColorMenuItem.cpp
	FindWindow.cpp
	ReplaceWindow.cpp
	StatusView.cpp
	StyledEditApp.cpp
	StyledEditView.cpp
	StyledEditWindow.cpp

	LIBS tracker shared translation translatorutils textencoding localestub
	RDEF StyledEdit.rdef
)

UsePrivateHeaders(StyledEdit textencoding tracker)
