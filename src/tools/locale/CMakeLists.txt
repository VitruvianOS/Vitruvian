if(BUILDTOOLS_MODE STREQUAL "1")

set (LOCALE_SOURCES ${CMAKE_SOURCE_DIR}/src/kits/locale/)

BuildModeApplication(linkcatkeys
	SOURCES
	linkcatkeys.cpp
	PlainTextCatalog.cpp
	${LOCALE_SOURCES}HashMapCatalog.cpp
	DefaultCatalog.cpp
	Catalog.cpp
	CatalogData.cpp
)

UsePrivateBuildHeaders(linkcatkeys shared)
UsePrivateHeaders(linkcatkeys locale)

endif()
