#add_library(root_bootstrap STATIC "")
#target_link_libraries(root_bootstrap pthread dl)
#set_target_properties(root_bootstrap PROPERTIES LINKER_LANGUAGE CXX)
#target_compile_definitions(root_bootstrap PRIVATE -DLIBBE_BOOTSTRAP=1)

add_library(root SHARED "")

target_link_libraries(root PRIVATE
    "-Wl,--whole-archive"
    "-Wl,--no-whole-archive"
)

file(GLOB_RECURSE privateHeaderFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

target_sources(root PRIVATE
    ${headerFiles}
    ${privateHeaderFiles}
)

set_target_properties(root PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(root PUBLIC dl pthread ${ZLIB_LIBRARIES})

add_subdirectory(kernel_linux)
add_subdirectory(libroot)
