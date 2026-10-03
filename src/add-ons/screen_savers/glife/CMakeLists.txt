# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(glife
	SOURCES
	GLifeConfig.cpp GLifeGrid.cpp GLifeSaver.cpp GLifeView.cpp

	LIBS be localestub opengl GL GLU
	RDEF GLife.rdef
)
