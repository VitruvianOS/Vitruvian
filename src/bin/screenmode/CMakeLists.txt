Application(screenmode
	SOURCES
	screenmode.cpp
	../../preferences/screen/multimon.cpp
	../../preferences/screen/ScreenMode.cpp

	LIBS accelerantscommon
	RDEF screenmode.rdef
)

UsePrivateHeaders(screenmode graphics/common)
