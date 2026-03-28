Application(TextSearch
	SOURCES
	ChangesIterator.cpp
	FileIterator.cpp
	GrepApp.cpp
	GrepListView.cpp
	Grepper.cpp
	GrepWindow.cpp
	InitialIterator.cpp
	Model.cpp
	TextSearch.cpp
	LIBS tracker textencoding shared localestub
	RDEF TextSearch.rdef
)
UsePrivateHeaders(TextSearch shared storage)
