# Store: user-facing catalog (apps, components, data) on BWebView.
# Install/remove reuse the PackageManager framework: the worker and
# apt backend sources are compiled in from ../packagemanager (one apt
# layer, not two).
Application(Store
	SOURCES
	main.cpp
	StoreWindow.cpp
	StoreCatalog.cpp
	../packagemanager/PackageWorker.cpp
	../packagemanager/AptBackend.cpp
	../packagemanager/PackageErrors.cpp
	../packagemanager/PackageInfo.cpp
	../packagemanager/ChangeSummaryWindow.cpp

	LIBS web vpackage apt-pkg tracker localestub

	RDEF Store.rdef
)

UsePrivateHeaders(Store shared net)

target_include_directories(Store PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/netservices
	${CMAKE_CURRENT_SOURCE_DIR}/../packagemanager
	${PROJECT_SOURCE_DIR}/headers
	${PROJECT_SOURCE_DIR}/headers/os
)

# Local page template + bundled catalog (fallback for the site copy); found via STORE_DATA_DIR or
# /system/data/store at runtime. Keep install() rules before DoCatalogs:
# DoCatalogs is a macro whose return() (no translations yet) would
# otherwise skip the rest of this file.
install(FILES
	data/index.html
	data/store.css
	data/store.js
	data/catalog.json
	DESTINATION /system/data/store
	COMPONENT runtime
)

# Last: early-returns when data/catalogs/apps/store is absent.
DoCatalogs("application/x-vnd.Vitruvian-Store" apps/store)
