find_library(PAM_LIBRARIES NAMES pam REQUIRED)
find_path(PAM_INCLUDE_DIRS NAMES security/pam_appl.h REQUIRED)

Server(janus_session

	SOURCES
	janus_session.cpp

	LIBS be ${SEAT_LIBRARIES} ${PAM_LIBRARIES}

	RDEF janus_session.rdef
)

target_include_directories(janus_session PRIVATE ${PAM_INCLUDE_DIRS})

UsePrivateHeaders(janus_session app input kernel system)
