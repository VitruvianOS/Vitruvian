TrackerAddOn(
	OpenTargetFolder SHARED

	SOURCES
	opentargetfolder.cpp

	LIBS be tracker

	RDEF opentargetfolder.rdef
)

UsePrivateHeaders(OpenTargetFolder tracker shared interface storage support)
