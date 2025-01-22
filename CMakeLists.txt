cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(VOS VERSION 0.2.0 LANGUAGES CXX C)

message(STATUS "")
message(STATUS "============= Build environment =============")
message(STATUS "CMAKE_SYSTEM:                    ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR:          ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_SIZEOF_VOID_P:             ${CMAKE_SIZEOF_VOID_P}")
message(STATUS "PLATFORM_BITS:                   ${PLATFORM_BITS}")
message(STATUS "CMAKE_C_FLAGS:                   ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS:                 ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_BUILD_TYPE:                ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_C_COMPILER:                ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER:              ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_AR:                        ${CMAKE_AR}")
message(STATUS "CMAKE_RANLIB:                    ${CMAKE_RANLIB}")
message(STATUS "=============================================")
message(STATUS "BUILDTOOLS_MODE:                 ${BUILDTOOLS_MODE}")
message(STATUS "BUILDTOOLS_DIR:                  ${BUILDTOOLS_DIR}")
message(STATUS "BUILD_TESTS:                     ${BUILD_TESTS}")
message(STATUS "=============================================")
message(STATUS "")

set(CMAKE_BUILD_TYPE Debug)

# TODO shouldn't we resolve an absolute path here? it kind of works
# only if buildtools is in the project source dir, which...we don't
# really want to constrain.
if(NOT BUILDTOOLS_DIR AND NOT BUILDTOOLS_MODE STREQUAL "1") 
	message( FATAL_ERROR "Buildtools path not defined!" )
endif()

include(build/engine.cmake)

add_subdirectory(src)

if(NOT BUILDTOOLS_MODE STREQUAL "1")
	include(build/packages.cmake)
endif()
