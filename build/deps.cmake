set(BUILD_ICU ON)
set(ICU_BUILD_VERSION 57.2)
add_subdirectory(external/icu-cmake)

find_package(ZLIB REQUIRED)
