add_executable(portinfo
	portinfo.cpp
)

target_link_libraries(portinfo PRIVATE be root freetype agg icon linprog)

add_executable(seminfo
	seminfo.cpp
)

target_link_libraries(seminfo PRIVATE be root)
