add_executable(Clock
	cl_view.cpp 
	cl_wind.cpp 
	clock.cpp 
)

target_link_libraries(Clock PRIVATE be root)
