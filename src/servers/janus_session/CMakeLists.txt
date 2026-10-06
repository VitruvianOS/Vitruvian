find_library(PAM_LIBRARIES NAMES pam REQUIRED)
find_path(PAM_INCLUDE_DIRS NAMES security/pam_appl.h REQUIRED)
find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

Server(janus_session

	SOURCES
	janus_session.cpp

	LIBS be ${SEAT_LIBRARIES} ${PAM_LIBRARIES} ${DRM_LIBRARIES} ${UDEV_LIBRARIES}

	RDEF janus_session.rdef

	INCLUDES
	${DRM_INCLUDE_DIR}/libdrm/
)

target_include_directories(janus_session PRIVATE ${PAM_INCLUDE_DIRS})

UsePrivateHeaders(janus_session app input kernel system)
