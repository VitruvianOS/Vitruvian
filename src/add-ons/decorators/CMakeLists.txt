# Window decorators for app_server's DecorManager. They subclass app_server
# classes, so they link against the app_server executable.
macro(DecoratorAddOnSettings name)
	# The menu label is the file name.
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_include_directories(${name} PRIVATE
		$<TARGET_PROPERTY:app_server,INCLUDE_DIRECTORIES>)
	target_compile_options(${name} PRIVATE
		-iquote ${PROJECT_SOURCE_DIR}/src/servers/app/)
	UsePrivateHeaders(${name} app_server app interface graphics
		graphics/common kernel support)
endmacro()

add_subdirectory(BeDecorator)
add_subdirectory(FlatDecorator)
add_subdirectory(MacDecorator)
add_subdirectory(WinDecorator)
