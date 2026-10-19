# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

AddOn(equalizer SHARED
	SOURCES
		Equalizer.cpp
		EqualizerUnit.cpp

	LIBS be media2
)

UsePrivateHeaders(equalizer media2 shared)
target_include_directories(equalizer PRIVATE
	${PIPEWIRE_INCLUDES}
)
target_link_libraries(equalizer PRIVATE
	${PIPEWIRE_LIBRARIES}
)
