TrackerAddOn(
	OpenTerminal SHARED

	SOURCES
	OpenTerminal.cpp

	LIBS be tracker

	RDEF OpenTerminal.rdef
)

UsePrivateHeaders(OpenTerminal tracker shared interface storage support)
