add_executable(portinfo
	portinfo.cpp
)

target_link_libraries(portinfo PRIVATE be root)

target_include_directories(portinfo PRIVATE
	"../../../headers/private/libroot/"
)


add_executable(seminfo
	seminfo.cpp
)

target_link_libraries(seminfo PRIVATE be root)

target_include_directories(seminfo PRIVATE
	"../../../headers/private/libroot/"
)
