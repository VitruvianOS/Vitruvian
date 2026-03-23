AddOn(
	root
	SHARED

	LIBS be_bootstrap rt dl pthread ${ZLIB_LIBRARIES} ${UDEV_LIBRARIES} ${BLKID_LIBRARIES}
)

# Inject VOS revision from the latest rev-N git tag at configure time.
execute_process(
	COMMAND git describe --tags --match "rev-*" --abbrev=0
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE VOS_REVISION_TAG
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
	RESULT_VARIABLE _git_result
)
if(_git_result EQUAL 0 AND VOS_REVISION_TAG)
	set(VOS_REVISION "${VOS_REVISION_TAG}")
else()
	set(VOS_REVISION "dev")
endif()
target_compile_definitions(root PRIVATE VOS_REVISION="${VOS_REVISION}")
message(STATUS "VOS revision:                    ${VOS_REVISION}")

find_library(BACKTRACE_LIBRARY backtrace)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT BACKTRACE_LIBRARY)
	message(FATAL_ERROR "Building in Debug mode requires libbacktrace-dev. Feel free to disable Debug build.")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_compile_definitions(root PRIVATE HAVE_LIBBACKTRACE)
	target_link_libraries(root PRIVATE ${BACKTRACE_LIBRARY})
	target_compile_options(root PRIVATE -fno-omit-frame-pointer)
	target_link_options(root PRIVATE -rdynamic)
endif()

# TODO: get rid of runtime loader
UsePrivateHeaders(root app shared libroot2 system kernel runtime_loader)

add_subdirectory(kernel)
add_subdirectory(libroot)
add_subdirectory(libroot2)
