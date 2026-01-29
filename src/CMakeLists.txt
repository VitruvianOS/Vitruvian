if(NOT BUILDTOOLS_MODE STREQUAL "1")

add_subdirectory(system)
add_subdirectory(bin)
add_subdirectory(tools)
add_subdirectory(libs)
add_subdirectory(kits)
add_subdirectory(servers)
add_subdirectory(add-ons)
add_subdirectory(apps)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_subdirectory(tests)
endif()

else()

add_subdirectory(build)
add_subdirectory(bin)

endif()
