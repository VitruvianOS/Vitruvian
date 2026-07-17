find_library(PAM_LIBRARIES NAMES pam REQUIRED)
find_path(PAM_INCLUDE_DIRS NAMES security/pam_appl.h REQUIRED)

Server(janus

	SOURCES
	janus.cpp

	LIBS be ${SEAT_LIBRARIES} ${PAM_LIBRARIES} systemd

	RDEF janus.rdef
)

target_include_directories(janus PRIVATE ${PAM_INCLUDE_DIRS})

UsePrivateHeaders(janus app input kernel system)

# janus_launch must not link libbe — it runs before registrar/app_server are up
add_executable(janus_launch janus_launch.cpp)
target_link_libraries(janus_launch PUBLIC root)
target_include_directories(janus_launch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(janus_launch PROPERTIES COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
UsePrivateHeaders(janus_launch app kernel)
CompileRdef(janus_launch janus_launch.rdef STAGING)
LinkRdefs(janus_launch)
