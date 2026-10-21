# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

AddOn(plugin_adapter SHARED
	SOURCES
		MediaPluginAdapter.cpp
		Lv2UIHost.cpp

	LIBS be media2 dl
)

UsePrivateHeaders(plugin_adapter media2 shared)
target_include_directories(plugin_adapter PRIVATE
	${PIPEWIRE_INCLUDES}
)
target_link_libraries(plugin_adapter PRIVATE
	${PIPEWIRE_LIBRARIES}
)

# LV2 hosting compiles only when lilv headers and library are present.
find_path(LILV_HEADER lilv/lilv.h PATH_SUFFIXES lilv-0)
find_library(LILV_LIBRARY NAMES lilv-0)
if(LILV_HEADER AND LILV_LIBRARY)
	target_include_directories(plugin_adapter PRIVATE
		${LILV_HEADER}
	)
	target_compile_definitions(plugin_adapter PRIVATE VOS_HAS_LILV=1)
	target_link_libraries(plugin_adapter PRIVATE ${LILV_LIBRARY})

	# The plugin's own UI is hosted through suil on top of lilv.
	find_path(SUIL_HEADER suil/suil.h PATH_SUFFIXES suil-0)
	find_library(SUIL_LIBRARY NAMES suil-0)
	if(SUIL_HEADER AND SUIL_LIBRARY)
		target_include_directories(plugin_adapter PRIVATE
			${SUIL_HEADER}
		)
		target_compile_definitions(plugin_adapter PRIVATE VOS_HAS_SUIL=1)
		target_link_libraries(plugin_adapter PRIVATE ${SUIL_LIBRARY})
	endif()
endif()
