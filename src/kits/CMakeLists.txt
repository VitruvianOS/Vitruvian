add_library(be SHARED "")
#target_link_libraries(be PRIVATE
#    "-Wl,--whole-archive"
#    "-Wl,--no-whole-archive"
#)
file(GLOB_RECURSE privateHeaderFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
target_sources(be PRIVATE
    ${headerFiles}
    ${privateHeaderFiles}
)
set_target_properties(be PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(be PRIVATE root linprog agg icu)
target_link_libraries(be PRIVATE icon)

add_subdirectory(app)
add_subdirectory(interface)
add_subdirectory(locale)
add_subdirectory(network)
add_subdirectory(shared)
add_subdirectory(storage)
add_subdirectory(support)
#add_subdirectory(tracker)
