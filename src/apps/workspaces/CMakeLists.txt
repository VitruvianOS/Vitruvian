Application(Workspaces
	SOURCES Workspaces.cpp
	LIBS shared localestub
	RDEF Workspaces.rdef
)
UsePrivateHeaders(Workspaces app interface shared)
DoCatalogs("x-vnd.Be-WORK" apps/workspaces)
