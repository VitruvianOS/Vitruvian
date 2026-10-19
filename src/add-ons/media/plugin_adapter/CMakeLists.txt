# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

AddOn(plugin_adapter SHARED
	SOURCES
		MediaPluginAdapter.cpp

	LIBS be media2 dl
)

UsePrivateHeaders(plugin_adapter media2 shared)
target_include_directories(plugin_adapter PRIVATE
	${PIPEWIRE_INCLUDES}
)
target_link_libraries(plugin_adapter PRIVATE
	${PIPEWIRE_LIBRARIES}
)

# LV2 path compiles only when lilv headers and library are present.
find_path(LILV_HEADER lilv/lilv.h)
find_library(LILV_LIBRARY NAMES lilv-0)
if(LILV_HEADER AND LILV_LIBRARY)
	target_include_directories(plugin_adapter PRIVATE
		${LILV_HEADER}/..
		${LV2_INCLUDES}
	)
	target_compile_definitions(plugin_adapter PRIVATE VOS_HAS_LILV=1)
	target_link_libraries(plugin_adapter PRIVATE ${LILV_LIBRARY})
endif()
