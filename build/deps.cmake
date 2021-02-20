macro( DeclareDependency name )
	set( _OPTIONS_ARGS )
	set( _ONE_VALUE_ARGS )
	set( _MULTI_VALUE_ARGS LIBRARIES INCLUDES PACKAGES )

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
endmacro()

set(BUILD_ICU ON)
set(ICU_BUILD_VERSION 57.2)
set(HEADERS_PATH_BASE "/usr/include")

add_subdirectory(external/icu-cmake)

find_package(ZLIB REQUIRED)

DeclareDependency(
	EXR
	LIBRARIES	"IlmImf;Half"
	PACKAGES	"libopenexr-dev"
	INCLUDES	"${HEADERS_PATH_BASE}/OpenEXR/"
)

DeclareDependency(
	FREETYPE
	LIBRARIES	"freetype"
	PACKAGES	"libfreetype6-dev"
)

DeclareDependency(
	GIF
	LIBRARIES	"gif"
	PACKAGES	"libgif-dev"
)

DeclareDependency(
	ICNS
	LIBRARIES	"icns"
	PACKAGES	"libicns-dev"
)

DeclareDependency(
	INPUT
	LIBRARIES	"input"
	PACKAGES	"libinput-dev"
)

DeclareDependency(
	JPEG
	LIBRARIES	"jpeg"
	PACKAGES	"libjpeg-dev"
)

DeclareDependency(
	PNG
	LIBRARIES	"png"
	PACKAGES	"libpng-dev"
	INCLUDES	"${HEADERS_PATH_BASE}/libpng/"
)

DeclareDependency(
	TIFF
	LIBRARIES	"tiff"
	PACKAGES	"libtiff-dev"
)

DeclareDependency(
	UDEV
	LIBRARIES	"udev"
	PACKAGES	"libudev-dev"
)

DeclareDependency(
	WEBP
	LIBRARIES	"webp"
	PACKAGES	"libwebp-dev"
)

file(WRITE build/deb_deps.sh "#!/bin/bash \nsudo apt install ${DEP_LIST} debootstrap squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin mtools -y")
