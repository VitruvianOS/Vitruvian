Application(AutoRaise
	SOURCES
	AutoRaiseApp.cpp
	AutoRaiseIcon.cpp
	settings.cpp
	LIBS localestub
	RDEF AutoRaise.rdef
)
UsePrivateHeaders(AutoRaise interface shared)
DoCatalogs("x-vnd.mmu.AutoRaise" apps/autoraise)
