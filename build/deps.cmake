macro( DeclareDependency name )
	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS LIBRARIES INCLUDES PACKAGES RUNTIMES )

	cmake_parse_arguments(
		_DECLAREDEPENDENCY
		"${_OPTIONS_ARGS}"
		"${_ONE_VALUE_ARGS}"
		"${_MULTI_VALUE_ARGS}"
		${ARGN}
	)

	set ("${name}_LIBRARIES" "${_DECLAREDEPENDENCY_LIBRARIES}")
	set ("${name}_INCLUDES" "${_DECLAREDEPENDENCY_INCLUDES}")

	list (APPEND PACKAGES_LIST "${_DECLAREDEPENDENCY_PACKAGES}")
	set (DEP_LIST "${DEP_LIST} ${_DECLAREDEPENDENCY_PACKAGES}")
	set (RUN_LIST ${RUN_LIST} ${_DECLAREDEPENDENCY_RUNTIMES})

endmacro()

add_subdirectory(external/backward-cpp)

DeclareDependency(
	GNUC
	RUNTIMES	"libc6 (>= 2.31-13)"
)

DeclareDependency(
	GNUCPP
	RUNTIMES	"libstdc++6 (>= 10.2.1-6)"
)

DeclareDependency(
	FLEX
	LIBRARIES	"fl"
	PACKAGES	"libfl-dev"
	RUNTIMES	"libfl2 (>= 2.6.4-8)"
)

DeclareDependency(
	FREETYPE
	LIBRARIES	"freetype"
	PACKAGES	"libfreetype6-dev"
	RUNTIMES	"libfreetype6 (>= 2.10.4)"
	INCLUDES	"${HEADERS_PATH_BASE}/freetype2/"
)

DeclareDependency(
	ICU
	LIBRARIES	"icu"
	PACKAGES	"libicu-dev"
	RUNTIMES	"libicu72 (>= 72.1-3)"
)

DeclareDependency(
	INPUT
	LIBRARIES	"input"
	PACKAGES	"libinput-dev"
	RUNTIMES	"libinput10 (>= 1.16.4-3)"
)

DeclareDependency(
	NOTO
	RUNTIMES	"fonts-noto-core;fonts-noto-extra;fonts-noto-mono"
)

DeclareDependency(
	DRM
	LIBRARIES	"drm"
	PACKAGES	"libdrm-dev"
	INCLUDES	"${HEADERS_PATH_BASE}/libdrm/"
)

DeclareDependency(
	GIF
	LIBRARIES	"gif"
	PACKAGES	"libgif-dev"
	RUNTIMES	"libgif7 (>= 5.1.9-2)"
)

DeclareDependency(
	TINFO
	RUNTIMES	"libtinfo6 (>= 6.2)"
)

DeclareDependency(
	UDEV
	LIBRARIES	"udev"
	PACKAGES	"libudev-dev"
	RUNTIMES	"libudev1 (>= 247.3-6)"
)

DeclareDependency(
	ZLIB
	LIBRARIES  "z"
	PACKAGES  "zlib1g-dev"
	RUNTIMES  "zlib1g (>= 1:1.2.11)"
)

#DeclareDependency(
#	ICNS
#	LIBRARIES	"icns"
#	PACKAGES	"libicns-dev"
#	RUNTIMES	"libicns1 (>= 0.8.1-3.1)"
#)

#DeclareDependency(
#	EXR
#	LIBRARIES	"IlmImf;Half"
#	PACKAGES	"libopenexr-dev"
#	RUNTIMES	"libopenexr25 (>= 2.5.4-2)"
#	INCLUDES	"${HEADERS_PATH_BASE}/OpenEXR/"
#)

#DeclareDependency(
#	JPEG
#	LIBRARIES	"jpeg"
#	PACKAGES	"libjpeg-dev"
#	RUNTIMES	"libjpeg62-turbo (>= 2.0.6-4)"
#)

#DeclareDependency(
#	PNG
#	LIBRARIES	"png"
#	PACKAGES	"libpng-dev"
#	RUNTIMES	"libpng16-16 (>= 1.6.37-3)"
#	INCLUDES	"${HEADERS_PATH_BASE}/libpng/"
#)

#DeclareDependency(
#	TIFF
#	LIBRARIES	"tiff"
#	PACKAGES	"libtiff-dev"
#	RUNTIMES	"libtiff6 (>= 4.5.0-1)"
#)

#DeclareDependency(
#	WEBP
#	LIBRARIES	"webp"
#	PACKAGES	"libwebp-dev"
#	RUNTIMES	"libwebp6 (>= 0.6.1-2.1)"
#)

file(WRITE generated.x86/build_deps.txt "${DEP_LIST}")
