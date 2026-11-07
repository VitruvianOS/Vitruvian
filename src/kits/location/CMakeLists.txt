target_sources(be PRIVATE
	LocationProvider.cpp
	private/GpsdParser.cpp
	private/GpsdSource.cpp
)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/private
)
