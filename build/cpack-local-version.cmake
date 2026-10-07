# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Local builds: replace the version placeholder with the packaging time.

string(TIMESTAMP _vos_stamp "%Y%m%d%H%M%S" UTC)
get_cmake_property(_vos_vars VARIABLES)
foreach(_vos_var IN LISTS _vos_vars)
	if(_vos_var MATCHES "^CPACK_DEBIAN_" AND "${${_vos_var}}" MATCHES "VOSLOCALSTAMP")
		string(REPLACE "VOSLOCALSTAMP" "${_vos_stamp}" ${_vos_var} "${${_vos_var}}")
	endif()
endforeach()
