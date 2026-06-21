# TODO: Tracker add-ons are loaded via load_add_on, so build SHARED.
# See translators/CMakeLists.txt for the same dual-mode caveat.
TrackerAddOn(
	FileType SHARED

	SOURCES
	FileType.cpp

	LIBS be tracker

	RDEF FileType.rdef
)

UsePrivateHeaders(FileType tracker shared interface storage support)
