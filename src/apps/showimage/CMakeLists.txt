Application(ShowImage
	SOURCES
	Filter.cpp
	ImageCache.cpp
	ImageFileNavigator.cpp
	PrintOptionsWindow.cpp
	ProgressWindow.cpp
	SelectionBox.cpp
	ShowImageApp.cpp
	ShowImageSettings.cpp
	ShowImageStatusView.cpp
	ShowImageView.cpp
	ShowImageWindow.cpp
	SupportingAppsMenu.cpp
	ToolBarIcons.cpp
	LIBS game tracker translation shared localestub
	RDEF ShowImage.rdef
)
UsePrivateHeaders(ShowImage tracker shared system kernel)
