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

# LV2 hosting through lilv; the plugin's own UI is hosted through suil.
target_include_directories(plugin_adapter PRIVATE
	${LILV_INCLUDES}
	${SUIL_INCLUDES}
	${LV2_INCLUDES}
	${HEADERS_PATH_BASE}
)
target_compile_definitions(plugin_adapter PRIVATE
	VOS_HAS_LILV=1
	VOS_HAS_SUIL=1
)
target_link_libraries(plugin_adapter PRIVATE
	${LILV_LIBRARIES}
	${SUIL_LIBRARIES}
)
