set(BUILD_ICU ON)
set(ICU_BUILD_VERSION 57.2)
set(LIBS_PATH_BASE "/usr/lib/x86_64-linux-gnu/")
set(HEADERS_PATH_BASE "/usr/include")

set(EXR_LIBRARIES "${LIBS_PATH_BASE}/libIlmImf.so;${LIBS_PATH_BASE}/libHalf.so")
set(EXR_INCLUDES "${HEADERS_PATH_BASE}/OpenEXR/")

set(GIF_LIBRARIES "${LIBS_PATH_BASE}/libgif.so")

set(ICNS_LIBRARIES "${LIBS_PATH_BASE}/libicns.so")

set(JPEG_LIBRARIES "${LIBS_PATH_BASE}/libjpeg.so")

set(PNG_LIBRARIES "${LIBS_PATH_BASE}/libpng.so")
set(PNG_INCLUDES "${HEADERS_PATH_BASE}/libpng/")

set(TIFF_LIBRARIES "${LIBS_PATH_BASE}/libtiff.so")

set(WEBP_LIBRARIES "${LIBS_PATH_BASE}/libwebp.so")


add_subdirectory(external/icu-cmake)

find_package(ZLIB REQUIRED)
