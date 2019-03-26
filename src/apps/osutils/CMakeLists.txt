add_executable(portinfo
	portinfo.cpp
)

target_link_libraries(portinfo PRIVATE be root)

add_executable(seminfo
	seminfo.cpp
)

target_link_libraries(seminfo PRIVATE be root)
