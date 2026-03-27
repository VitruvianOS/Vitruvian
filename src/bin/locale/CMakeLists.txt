# collectcatkeys uses Tracker's old RegExp API which conflicts with headers/private/shared/RegExp.h
#Application(collectcatkeys
#	SOURCES
#	collectcatkeys.cpp
#	../../kits/tracker/RegExp.cpp
#
#	LIBS localestub
#)

Application(linkcatkeys
	SOURCES
	linkcatkeys.cpp
)

Application(dumpcatalog
	SOURCES
	dumpcatalog.cpp
)

Application(locale
	SOURCES
	locale.cpp
)

#UsePrivateHeaders(collectcatkeys locale shared)
UsePrivateHeaders(linkcatkeys locale shared)
UsePrivateHeaders(dumpcatalog locale shared)
UsePrivateHeaders(locale locale shared)
