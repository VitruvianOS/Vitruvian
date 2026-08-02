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
        return()
    endif()

    # For native builds (same arch as host), don't set sysroot
    # Let CMAKE_FIND_ROOT_PATH handle library search instead
    if(NOT CMAKE_CROSSCOMPILING)
        set(CMAKE_SYSROOT "")
    else()
        set(CMAKE_SYSROOT "${VITRUVIAN_CHROOT_PATH}")
    endif()

    set(CMAKE_FIND_ROOT_PATH "${VITRUVIAN_CHROOT_PATH}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    # Resolve libraries/headers/packages ONLY from the chroot sysroot, never the
    # host. The chroot carries every build -dev package (see packages.sh
    # get_dev_packages), so host fallback only causes harm: on a host with a
    # newer glibc than the chroot, a host .so (e.g. libpam, libm) drags in
    # symbol versions the target glibc lacks (GLIBC_2.43 vs trixie's 2.41) and
    # the binary fails to start. PROGRAM stays NEVER — build tools are the host's.
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    # Anchor find_package(ICU) to the chroot. The global -isystem below makes
    # <unicode/*> resolve to the chroot's ICU (76 on trixie), so its libraries
    # must come from the chroot too. Without this, on a host whose ICU major
    # differs (e.g. Ubuntu 26.04 ships ICU 78), find_package resolves ICU_LIBRARIES
    # to the host .so and every icu_NN-namespaced symbol goes undefined at link.
    set(ICU_ROOT "${VITRUVIAN_CHROOT_PATH}/usr")

    set(HEADERS_PATH_BASE "${VITRUVIAN_CHROOT_PATH}/usr/include"
        CACHE PATH "Base path for system headers")

    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${VITRUVIAN_CHROOT_PATH}")
    set(ENV{PKG_CONFIG_LIBDIR}
        "${VITRUVIAN_CHROOT_PATH}/usr/lib/${VITRUVIAN_MULTIARCH_TRIPLE}/pkgconfig:${VITRUVIAN_CHROOT_PATH}/usr/lib/pkgconfig:${VITRUVIAN_CHROOT_PATH}/usr/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "")

    include_directories(SYSTEM "${VITRUVIAN_CHROOT_PATH}/usr/include/${VITRUVIAN_MULTIARCH_TRIPLE}")
    include_directories(SYSTEM "${VITRUVIAN_CHROOT_PATH}/usr/include")

    link_directories(
        "${VITRUVIAN_CHROOT_PATH}/usr/lib/${VITRUVIAN_MULTIARCH_TRIPLE}"
        "${VITRUVIAN_CHROOT_PATH}/lib/${VITRUVIAN_MULTIARCH_TRIPLE}"
        "${VITRUVIAN_CHROOT_PATH}/usr/lib"
    )

    # Indirect dep resolution (e.g. libfoo.so pulled in by another linked .so)
    # ignores -L and consults only -rpath-link, DT_RUNPATH, or ld defaults.
    # Without this, ld silently falls through to the host's /usr/lib when
    # verifying NEEDED entries, which works only when the host's library
    # versions happen to match the chroot's (e.g. ICU 76 on Debian trixie).
    # Only the multiarch system dirs go here — NOT ${chroot}/usr/lib, which is
    # where a prior image build installs VOS's own libs (libbe, libmedia2, …).
    # Including it would let a stale installed copy shadow the freshly built
    # one during indirect resolution; VOS libs must come from the build tree
    # (per-target rpath-link entries CMake adds for src/kits/*).
    set(_chroot_rpath_link "${VITRUVIAN_CHROOT_PATH}/usr/lib/${VITRUVIAN_MULTIARCH_TRIPLE}:${VITRUVIAN_CHROOT_PATH}/lib/${VITRUVIAN_MULTIARCH_TRIPLE}")
    add_link_options("-Wl,-rpath-link=${_chroot_rpath_link}")

    # Link the implicit C runtime (crt*.o, libc, libm) from the chroot so glibc
    # symbol *versions* bind to the target's glibc, not the host's. On a host
    # with a newer glibc than the chroot (e.g. Ubuntu 26.04 = 2.43 vs trixie =
    # 2.41), plain math symbols like sqrtf/atan2f would otherwise pin to
    # sqrtf@GLIBC_2.43 and fail at runtime with "version GLIBC_2.43 not found".
    # libstdc++ still comes from the host gcc (its GLIBCXX use here stays within
    # what the chroot provides), so we scope this to the C runtime via --sysroot.
    add_link_options("--sysroot=${VITRUVIAN_CHROOT_PATH}")

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
