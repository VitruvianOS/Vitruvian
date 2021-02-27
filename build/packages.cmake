 #  Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

function( ImageInclude path )
	foreach(arg IN LISTS ARGN)
		install(TARGETS ${arg}
			COMPONENT ${path}
			RUNTIME DESTINATION ${path}
			LIBRARY DESTINATION ${path}
			DESTINATION ${path}
		)
	endforeach()
endfunction()

function( ImageIncludeFile source dest )
	install(FILES ${source} DESTINATION ${dest})
endfunction()

function( ImageIncludeDir source dest )
	install(DIRECTORY ${source} DESTINATION ${dest})
endfunction()

function( ImageCreateDir dest )
	install(DIRECTORY DESTINATION ${dest})
endfunction()

include(build/baseimage.cmake)
include(build/profiles/default)


set(CORE_DEPS "fonts-noto-core, fonts-noto-extra, fonts-noto-mono, libc6 (>= 2.28), libfl2 (>= 2.5.33), libfreetype6 (>= 2.2.1), libgif7 (>= 5.1.4), libicns1 (>= 0.8.1), libinput10 (>= 0.15.0), libncurses6 (>= 6), libopenexr23 (>= 2.2.1), libpng16-16 (>= 1.6.36),libstdc++6 (>= 5.2), libtiff5 (>= 4.1.0), libtinfo6 (>= 6), libudev1 (>= 183), libwebp6 (>= 0.6.1), zlib1g (>= 1:1.1.4)")

set(CPACK_DEBIAN_PACKAGE_DEPENDS ${CORE_DEPS})
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Vitruvian Project")
INCLUDE(CPack)
