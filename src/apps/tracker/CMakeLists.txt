Application(
	Tracker

	SOURCES
	main.cpp

	LIBS localestub shared tracker
	RDEF Tracker.rdef
)

UsePrivateHeaders(Tracker shared tracker)
DoCatalogs("x-vnd.Be-TRAK" apps/tracker)
