 #  Copyright 2019-2020, Dario Casalinuovo. All rights reserved.
 #  Distributed under the terms of the LGPL License.

function( ImageInclude target path )
	install(TARGETS ${target}
		COMPONENT ${path}
		RUNTIME DESTINATION ${path}
		LIBRARY DESTINATION ${path}
		DESTINATION ${path}
	)
endfunction()


ImageInclude(
	registrar "/system/servers"
)

ImageInclude(
	app_server "/system/servers"
)

ImageInclude(
	StyledEdit "/system/apps"
)


#include(build/profiles/default)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "KK")
INCLUDE(CPack)
