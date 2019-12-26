
add_executable(ScreenTest
	ScreenTest.cpp
)

target_link_libraries(ScreenTest PRIVATE be root)

Application(
	LookTest

	SOURCES
	look/Look.cpp
	INCLUDES
	"../../../../headers/private/interface"
)

target_link_libraries(LookTest PRIVATE tracker)
