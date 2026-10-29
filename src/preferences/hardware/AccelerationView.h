/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCELERATION_VIEW_H
#define _ACCELERATION_VIEW_H


#include <View.h>


// The Acceleration tab of the Hardware preflet: OpenGL renderer and GPU
// settings, and the CPU/memory overclock controls.
class AccelerationView : public BView {
public:
							AccelerationView();
	virtual					~AccelerationView();
};


#endif	// _ACCELERATION_VIEW_H
