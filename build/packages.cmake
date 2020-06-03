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

include(build/profiles/default)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "KK")
INCLUDE(CPack)
