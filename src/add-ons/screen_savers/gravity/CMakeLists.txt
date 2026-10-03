# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(gravity
	SOURCES
	ConfigView.cpp Gravity.cpp GravitySource.cpp GravityView.cpp Particle.cpp RainbowItem.cpp main.cpp

	LIBS be localestub opengl GL GLU
	RDEF Gravity.rdef
)
