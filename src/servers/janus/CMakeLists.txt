Server(janus

	SOURCES
	janus.cpp

	LIBS be ${SEAT_LIBRARIES}

	RDEF janus.rdef
)

UsePrivateHeaders(janus app input kernel)

# janus_launch must not link libbe — it runs before registrar/app_server are up
add_executable(janus_launch janus_launch.cpp)
target_link_libraries(janus_launch PUBLIC root)
target_include_directories(janus_launch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(janus_launch PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
UsePrivateHeaders(janus_launch app kernel)
CompileRdef(janus_launch janus_launch.rdef STAGING)
