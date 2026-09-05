set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(VITRUVIAN_TARGET_ARCH arm64)


set(TOOLCHAIN_PREFIX "aarch64-linux-gnu-")
find_program(BINUTILS_PATH ${TOOLCHAIN_PREFIX}gcc NO_CACHE)

if (NOT BINUTILS_PATH)
    message(FATAL_ERROR "ARM64 GCC toolchain not found")
endif ()

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_AR ${TOOLCHAIN_PREFIX}gcc-ar)
set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}gcc-ranlib)

# Don't search host paths for programs, only use specified compiler
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SYSROOT ${VITRUVIAN_CHROOT_PATH})

set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

include_directories(SYSTEM "${VITRUVIAN_CHROOT_PATH}/usr/include")

# Multiarch dirs MUST come before any host default dir so "-l apt-pkg"
# resolves against the chroot's Debian trixie libapt-pkg.so.7.0, not the
# host runner's Ubuntu 24.04 libapt-pkg.so.6.0 (which only grows
# pkgCache::FindPkg(APT::StringView) and misses the std::string_view
# overload the tree calls). Mirrors arm64_raspberry.cmake / amd64.cmake.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-flto -L${VITRUVIAN_CHROOT_PATH}/lib/aarch64-linux-gnu -L${VITRUVIAN_CHROOT_PATH}/usr/lib/aarch64-linux-gnu -L${VITRUVIAN_CHROOT_PATH}/usr/lib -L${VITRUVIAN_CHROOT_PATH}/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
