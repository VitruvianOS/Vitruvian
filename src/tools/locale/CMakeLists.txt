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

# linkcatkeys needs the REAL BCatalog class definition (headers/os/locale)
# — the buildtools-mode stub at headers/build/os/locale/Catalog.h only
# defines translation macros. BEFORE ordering puts the real header ahead
# of anything the buildtools stub would shadow.
target_include_directories(linkcatkeys BEFORE PRIVATE
	"${CMAKE_SOURCE_DIR}/headers/os/locale")

endif()
