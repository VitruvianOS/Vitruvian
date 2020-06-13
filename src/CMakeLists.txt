add_subdirectory(system)

add_subdirectory(bin)
add_subdirectory(tools)

add_subdirectory(libs)

add_subdirectory(kits)
add_subdirectory(servers)
add_subdirectory(add-ons)

add_subdirectory(apps)

if(BUILD_TESTS STREQUAL "1")
	add_subdirectory(tests)
endif()
