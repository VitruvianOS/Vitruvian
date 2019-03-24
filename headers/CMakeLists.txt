file(GLOB_RECURSE headerFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(headers_install_dir /headers/os)
install(FILES ${headerFiles} DESTINATION ${headers_install_dir})

set(headerFiles ${headerFiles} PARENT_SCOPE)
