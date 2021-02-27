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

string(REPLACE ";" "," RESULT "${RUN_LIST}")
message(STATUS "RESULT: ${RESULT}")
set(CORE_DEPS "${RESULT}")

set(CPACK_DEBIAN_PACKAGE_DEPENDS ${CORE_DEPS})
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "The Vitruvian Project")
INCLUDE(CPack)
