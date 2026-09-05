# build/chroot.cmake
# Chroot sysroot configuration for Vitruvian.
# Included by defs.cmake. Sets HEADERS_PATH_BASE and library search paths.

option(VITRUVIAN_CHROOT_BUILD "Build using chroot sysroot for headers/libraries" OFF)
set(VITRUVIAN_CHROOT_PATH "" CACHE PATH "Path to chroot directory (default: <build>/image_tree/chroot)")

if(VITRUVIAN_CHROOT_BUILD)
    if(NOT VITRUVIAN_CHROOT_PATH)
        set(VITRUVIAN_CHROOT_PATH "${CMAKE_BINARY_DIR}/image_tree/chroot")
    endif()

    if(NOT EXISTS "${VITRUVIAN_CHROOT_PATH}")
        message(FATAL_ERROR
            "Chroot build enabled but chroot not found at: ${VITRUVIAN_CHROOT_PATH}\n"
            "Run build/scripts/setupenv.sh first, or pass -DVITRUVIAN_CHROOT_PATH=<path>.")
    endif()

    message(STATUS "Chroot build: ${VITRUVIAN_CHROOT_PATH}")

    set(VITRUVIAN_MULTIARCH_TRIPLE "x86_64-linux-gnu")
    if(DEFINED VITRUVIAN_TARGET_ARCH)
        if(VITRUVIAN_TARGET_ARCH STREQUAL "arm64")
            set(VITRUVIAN_MULTIARCH_TRIPLE "aarch64-linux-gnu")
        elseif(VITRUVIAN_TARGET_ARCH STREQUAL "arm" OR VITRUVIAN_TARGET_ARCH STREQUAL "arm32")
            set(VITRUVIAN_MULTIARCH_TRIPLE "arm-linux-gnueabihf")
        elseif(VITRUVIAN_TARGET_ARCH STREQUAL "riscv64")
            set(VITRUVIAN_MULTIARCH_TRIPLE "riscv64-linux-gnu")
        endif()
    endif()

    if(CMAKE_CROSSCOMPILING)
        message(STATUS "Using toolchain file")
        set(HEADERS_PATH_BASE "${VITRUVIAN_CHROOT_PATH}/usr/include"
            CACHE PATH "Base path for system headers")
        if(NOT KERNEL_RELEASE)
            message(FATAL_ERROR "KERNEL_RELEASE not set. Pass -DKERNEL_RELEASE=<version>.")
        endif()
        set(VITRUVIAN_KERNEL_HEADERS
            "${VITRUVIAN_CHROOT_PATH}/usr/src/linux-headers-${KERNEL_RELEASE}"
            CACHE PATH "Kernel headers for nexus-dkms")
        # Fall through so cross builds inherit the same chroot library
        # search paths as native builds (link_directories + rpath-link
        # below). The early return here used to skip them, which made the
        # cross linker bind system libs like -lapt-pkg against the host's
        # multiarch dir (SONAME libapt-pkg.so.6.0 on Ubuntu 24.04) while
        # the chroot carries Debian trixie's libapt-pkg.so.7.0 — the final
        # executable link then could not resolve the NEEDED entry at all.
        # The toolchain file already sets CMAKE_SYSROOT for cross builds,
        # so do not override it with the native empty value below. Cross
        # toolchains also already set CMAKE_FIND_ROOT_PATH_MODE_* to ONLY,
        # so the mode assignments below must stay native-only as well.
    else()
        set(CMAKE_SYSROOT "")
    endif()

    set(CMAKE_FIND_ROOT_PATH "${VITRUVIAN_CHROOT_PATH}")
    if(NOT CMAKE_CROSSCOMPILING)
        set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
        set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
        set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
        set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    endif()

    set(HEADERS_PATH_BASE "${VITRUVIAN_CHROOT_PATH}/usr/include"
        CACHE PATH "Base path for system headers")

    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${VITRUVIAN_CHROOT_PATH}")
    set(ENV{PKG_CONFIG_LIBDIR}
        "${VITRUVIAN_CHROOT_PATH}/usr/lib/${VITRUVIAN_MULTIARCH_TRIPLE}/pkgconfig:${VITRUVIAN_CHROOT_PATH}/usr/lib/pkgconfig:${VITRUVIAN_CHROOT_PATH}/usr/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "")

    include_directories(SYSTEM "${VITRUVIAN_CHROOT_PATH}/usr/include/${VITRUVIAN_MULTIARCH_TRIPLE}")
    include_directories(SYSTEM "${VITRUVIAN_CHROOT_PATH}/usr/include")

    # link_directories() only affects targets created in the *current*
    # directory, not targets defined in add_subdirectory() trees (e.g.
    # src/apps/packagemanager). A plain "-l apt-pkg" there would therefore
    # miss the chroot multiarch dir and bind the host compiler's default
    # library dir (Ubuntu 24.04 libapt-pkg.so.6.0, whose
    # pkgCache::FindPkg takes APT::StringView) instead of the chroot's
    # Debian trixie libapt-pkg.so.7.0 (pkgCache::FindPkg(std::string_view)),
    # producing "undefined reference to pkgCache::FindPkg(...)" at link time.
    # add_link_options() propagates to every target in the directory tree, so
    # the chroot search dirs are unconditionally on the link line.
    set(_chroot_link_dirs
        "-L${VITRUVIAN_CHROOT_PATH}/usr/lib/${VITRUVIAN_MULTIARCH_TRIPLE}"
        "-L${VITRUVIAN_CHROOT_PATH}/lib/${VITRUVIAN_MULTIARCH_TRIPLE}"
        "-L${VITRUVIAN_CHROOT_PATH}/usr/lib"
    )
    add_link_options(${_chroot_link_dirs})

    # Indirect dep resolution (e.g. libfoo.so pulled in by another linked .so)
    # ignores -L and consults only -rpath-link, DT_RUNPATH, or ld defaults.
    # Without this, ld silently falls through to the host's /usr/lib when
    # verifying NEEDED entries, which works only when the host's library
    # versions happen to match the chroot's (e.g. ICU 76 on Debian trixie).
    set(_chroot_rpath_link "${VITRUVIAN_CHROOT_PATH}/usr/lib/${VITRUVIAN_MULTIARCH_TRIPLE}:${VITRUVIAN_CHROOT_PATH}/lib/${VITRUVIAN_MULTIARCH_TRIPLE}:${VITRUVIAN_CHROOT_PATH}/usr/lib")
    add_link_options("-Wl,-rpath-link=${_chroot_rpath_link}")

    set(CMAKE_SKIP_RPATH TRUE)

    if(NOT KERNEL_RELEASE)
        message(FATAL_ERROR "KERNEL_RELEASE not set. Pass -DKERNEL_RELEASE=<version>.")
    endif()
    set(VITRUVIAN_KERNEL_HEADERS
        "${VITRUVIAN_CHROOT_PATH}/usr/src/linux-headers-${KERNEL_RELEASE}"
        CACHE PATH "Kernel headers for nexus-dkms")

else()
    message(STATUS "Host build (using system libraries)")

    set(HEADERS_PATH_BASE "/usr/include"
        CACHE PATH "Base path for system headers")

    if(NOT VITRUVIAN_MULTIARCH_TRIPLE)
        if(CMAKE_LIBRARY_ARCHITECTURE)
            set(VITRUVIAN_MULTIARCH_TRIPLE "${CMAKE_LIBRARY_ARCHITECTURE}")
        else()
            set(VITRUVIAN_MULTIARCH_TRIPLE "x86_64-linux-gnu")
        endif()
    endif()

    if(KERNEL_RELEASE)
        set(VITRUVIAN_KERNEL_HEADERS "/lib/modules/${KERNEL_RELEASE}/build"
            CACHE PATH "Kernel headers for nexus-dkms")
    endif()
endif()
