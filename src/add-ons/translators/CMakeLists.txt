add_subdirectory(shared)

add_subdirectory(bmp)
# exr: source is OpenEXR 2.x era (uses halfFunction.h); doesn't build against
# OpenEXR 3.x in modern Debian. Skip until ported.
#add_subdirectory(exr)
add_subdirectory(gif)
add_subdirectory(hvif)
add_subdirectory(icns)
add_subdirectory(ico)
add_subdirectory(jpeg)
# jpeg2000: needs libjasper (removed from Debian); skip until ported to OpenJPEG
#add_subdirectory(jpeg2000)
add_subdirectory(pcx)
add_subdirectory(png)
add_subdirectory(ppm)
add_subdirectory(psd)
add_subdirectory(raw)
add_subdirectory(rtf)
add_subdirectory(sgi)
add_subdirectory(stxt)
add_subdirectory(tga)
add_subdirectory(tiff)
add_subdirectory(webp)
add_subdirectory(wonderbrush)
# avif: no CMakeLists.txt yet, skipped
