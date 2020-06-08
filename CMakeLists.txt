cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(VOS VERSION 0.1.0 LANGUAGES CXX C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# Defaults
set(CMAKE_COLOR_MAKEFILE ON)

# Ensure env
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Create a separate build directory and run 'cmake path_to_vos [options]' there.")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "Only Ninja generator is supported: cmake ... -GNinja")
endif()
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Linux is supported.")
endif()

# Color diagnostics
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    SET(DIAG_FLAGS "-fcolor-diagnostics")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    SET(DIAG_FLAGS "-fdiagnostics-color")
endif()

# Build type flags.
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g0")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

# Common compiler flags.
set(COMMON_FLAGS "-include LinuxBuildCompatibility.h -Wall \
	-Wpointer-arith -Wcast-align -Wsign-compare -Wstrict-aliasing \
	-Wno-multichar -fPIC ${DIAG_FLAGS}")

# TODO: We want ideally to remove -Wno-deprecated at some point.

# Set flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} \
	-Woverloaded-virtual -Wno-ctor-dtor-privacy \
	-fexceptions -fpermissive -Wno-deprecated")

# V\OS defines
set(GLOBAL_CFLAGS
	#-DOPENSSL_ENABLED
)

# Deps
set(BUILD_ICU ON)
set(ICU_BUILD_VERSION 57.2)
add_subdirectory(external/icu-cmake)

find_package(ZLIB REQUIRED)

# Headers
set(
	PUBLIC_HEADERS

	"headers/os"
	"headers/os/app"
	"headers/os/drivers"
	"headers/os/kernel"
	"headers/os/support"
	"headers/os/storage"
	"headers/os/translation"

	"headers/os/config"
	"headers/os/add-ons/file_system"
	"headers/os/add-ons/graphics"
	"headers/os/add-ons/registrar"
	"headers/os/app"
	"headers/os/arch/x86_64"
	"headers/os/device"
	"headers/os/drivers/bus"
	"headers/os/game"
	"headers/os/interface"
	"headers/os/kernel"
	"headers/os/locale/"
	"headers/os/locale/tools"
	"headers/os/mail"
	"headers/os/net"
	"headers/os/storage"
	"headers/os/support"
	"headers/os/posix/compat/sys"

	"headers/tools/cppunit"

	"headers/posix"
)

set(
	PRIVATE_HEADERS

	"headers/private/"

	"headers/private/input"
	"headers/private/libroot"
	"headers/private/print"
	"headers/private/shared"
	"headers/private/storage"
	"headers/private/system"
	"headers/private/tracker"

	"headers/private/system/arch/x86/"
	"headers/private/kernel/arch/x86/"

	"headers/libs/agg/"
	"headers/libs/linprog/"

	"src/kits/tracker/"
	"src/system/libroot/os/"
)

set(
	BUILD_HEADERS
	"headers/build"
	"headers/build/config_headers"
	"headers/config"
)

# Default include directories.
include_directories(
	AFTER
	"headers"
	${BUILD_HEADERS}
	${PRIVATE_HEADERS}
	${PUBLIC_HEADERS}
)

# Log some information.
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
message(STATUS "")

#include support functions
include(build/engine.cmake)

# Sub directories
#add_subdirectory(headers)
add_subdirectory(src)

include(build/packages.cmake)
