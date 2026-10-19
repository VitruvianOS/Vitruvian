Application(Camera_prefs
	SOURCES
	Camera.cpp
	CameraPreviewView.cpp
	CameraWindow.cpp
	LIBS be shared localestub media2
	RDEF Camera.rdef
)

target_include_directories(Camera_prefs PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/media2
	${PIPEWIRE_INCLUDES}
	${SPA_INCLUDES}
)
UsePrivateHeaders(Camera_prefs interface preferences shared)
set_target_properties(Camera_prefs PROPERTIES OUTPUT_NAME Camera)
DoCatalogs("x-vnd.vos-Camera" preferences/camera)
