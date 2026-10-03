Application(Camera
	SOURCES
	Camera.cpp
	CameraPreviewView.cpp
	CameraWindow.cpp
	LIBS be shared localestub media2
	RDEF Camera.rdef
)

target_include_directories(Camera PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/src/kits/media2
	${PIPEWIRE_INCLUDES}
	${SPA_INCLUDES}
)
UsePrivateHeaders(Camera interface preferences shared)
DoCatalogs("x-vnd.vos-Camera" preferences/camera)
