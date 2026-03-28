Application(DiskProbe
	SOURCES
	AttributeWindow.cpp
	DataEditor.cpp
	DataView.cpp
	DiskProbe.cpp
	FileWindow.cpp
	FindWindow.cpp
	OpenWindow.cpp
	ProbeView.cpp
	ProbeWindow.cpp
	TypeEditors.cpp
	${PROJECT_SOURCE_DIR}/src/kits/shared/ExpressionParser.cpp
	LIBS tracker translation localestub mapm
	RDEF DiskProbe.rdef
)
UsePrivateHeaders(DiskProbe interface shared)
target_include_directories(DiskProbe PRIVATE ${PROJECT_SOURCE_DIR}/src/libs/mapm)
