Application(FileTypes
	SOURCES
	ApplicationTypesWindow.cpp
	ApplicationTypeWindow.cpp
	AttributeListView.cpp
	AttributeWindow.cpp
	DropTargetListView.cpp
	ExtensionWindow.cpp
	FileTypes.cpp
	FileTypesWindow.cpp
	FileTypeWindow.cpp
	IconView.cpp
	MimeTypeListView.cpp
	NewFileTypeWindow.cpp
	PreferredAppMenu.cpp
	StringView.cpp
	TypeListWindow.cpp
	LIBS tracker localestub
	RDEF FileTypes.rdef FileTypes.icons.rdef
)
target_include_directories(FileTypes PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/icon)
