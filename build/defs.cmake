set(CMAKE_COLOR_MAKEFILE ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Cannot use source dir as binary dir. Create a separate build directory.")
endif()

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "This project support only Ninja generator: cmake .. -GNinja")
endif()

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Linux is supported as host operating system.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# Add Color diagnostics
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    SET(DIAG_FLAGS "-fcolor-diagnostics")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    SET(DIAG_FLAGS "-fdiagnostics-color")
endif()

# Build flags
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g0")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

# Set common flags
set(COMMON_FLAGS "-Wall \
	-Wpointer-arith -Wcast-align -Wsign-compare -Wstrict-aliasing \
	-Wno-multichar -fPIC ${DIAG_FLAGS}")

# Set flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} \
	-Woverloaded-virtual -Wno-ctor-dtor-privacy \
	-fexceptions -fpermissive -Wno-deprecated")

# TODO: We want ideally to remove -Wno-deprecated at some point.

# V\OS Global defines
set(GLOBAL_CFLAGS
	#-DOPENSSL_ENABLED
)
