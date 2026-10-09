/*
 * Copyright 2026, The Vitruvian Project
 * Distributed under the terms of the MIT License.
 */
#ifndef PANEL_ORIENTATION_TRANSFORM_H
#define PANEL_ORIENTATION_TRANSFORM_H


#include <string.h>

#include <SupportDefs.h>


// Matches the kernel's DRM_MODE_PANEL_ORIENTATION_* values and the
// "screen_orientation" field on B_SCREEN_BOUNDS_CHANGED.
enum {
	B_PANEL_ORIENTATION_NORMAL      = 0,
	B_PANEL_ORIENTATION_UPSIDE_DOWN = 1,
	B_PANEL_ORIENTATION_LEFT_UP     = 2,
	B_PANEL_ORIENTATION_RIGHT_UP    = 3
};


// Matches the "screen_reflection" field on B_SCREEN_BOUNDS_CHANGED. X and Y
// are independent axes, so BOTH is X | Y and the values double as flags.
enum {
	B_PANEL_REFLECTION_NONE = 0,
	B_PANEL_REFLECTION_X    = 1,
	B_PANEL_REFLECTION_Y    = 2,
	B_PANEL_REFLECTION_BOTH = B_PANEL_REFLECTION_X | B_PANEL_REFLECTION_Y
};


// Outcome of reading one of the VOS_PANEL_* overrides.
enum panel_env_status {
	B_PANEL_ENV_UNSET,
	B_PANEL_ENV_SET,
	B_PANEL_ENV_INVALID
};


// The overrides are the only way to exercise a transformed display on a
// backend that reports no panel orientation, which is every virtual one.
// Parsing lives here so the compositor and the greeter cannot drift in what
// they accept; each caller keeps its own diagnostics.
static inline panel_env_status
parse_panel_orientation(const char* value, int32& outOrientation)
{
	if (value == NULL || value[0] == '\0')
		return B_PANEL_ENV_UNSET;

	if (strcmp(value, "normal") == 0 || strcmp(value, "0") == 0)
		outOrientation = B_PANEL_ORIENTATION_NORMAL;
	else if (strcmp(value, "upside-down") == 0 || strcmp(value, "1") == 0)
		outOrientation = B_PANEL_ORIENTATION_UPSIDE_DOWN;
	else if (strcmp(value, "left-up") == 0 || strcmp(value, "2") == 0)
		outOrientation = B_PANEL_ORIENTATION_LEFT_UP;
	else if (strcmp(value, "right-up") == 0 || strcmp(value, "3") == 0)
		outOrientation = B_PANEL_ORIENTATION_RIGHT_UP;
	else
		return B_PANEL_ENV_INVALID;

	return B_PANEL_ENV_SET;
}


static inline panel_env_status
parse_panel_reflection(const char* value, int32& outReflection)
{
	if (value == NULL || value[0] == '\0')
		return B_PANEL_ENV_UNSET;

	if (strcmp(value, "none") == 0 || strcmp(value, "0") == 0)
		outReflection = B_PANEL_REFLECTION_NONE;
	else if (strcmp(value, "x") == 0 || strcmp(value, "1") == 0)
		outReflection = B_PANEL_REFLECTION_X;
	else if (strcmp(value, "y") == 0 || strcmp(value, "2") == 0)
		outReflection = B_PANEL_REFLECTION_Y;
	else if (strcmp(value, "both") == 0 || strcmp(value, "3") == 0)
		outReflection = B_PANEL_REFLECTION_BOTH;
	else
		return B_PANEL_ENV_INVALID;

	return B_PANEL_ENV_SET;
}


// Rotates a normalized (0..1) point in physical panel space into
// normalized logical (landscape, post-rotation) desktop space. Reflection
// composes as a post-step here because the forward mapping reflects before
// rotating, and this is the inverse: un-rotate first, un-reflect second.
static inline void
rotate_panel_point(int32 orientation, float u, float v, float& outU,
	float& outV, int32 reflection = B_PANEL_REFLECTION_NONE)
{
	float ru, rv;
	switch (orientation) {
		case B_PANEL_ORIENTATION_UPSIDE_DOWN:
			ru = 1.0f - u;
			rv = 1.0f - v;
			break;
		case B_PANEL_ORIENTATION_LEFT_UP:
			ru = 1.0f - v;
			rv = u;
			break;
		case B_PANEL_ORIENTATION_RIGHT_UP:
			ru = v;
			rv = 1.0f - u;
			break;
		default:
			ru = u;
			rv = v;
			break;
	}
	outU = (reflection & B_PANEL_REFLECTION_X) != 0 ? 1.0f - ru : ru;
	outV = (reflection & B_PANEL_REFLECTION_Y) != 0 ? 1.0f - rv : rv;
}


#ifndef VOS_DELTA_MAP_REVERSED
#	define VOS_DELTA_MAP_REVERSED 0
#endif


// Rotates a relative motion or scroll delta the same way, without the
// translation component (pure vector rotation) — keep this in the same
// direction convention as rotate_panel_point() or the pointer moves
// correctly on one axis and inverted on the other.
static inline void
rotate_panel_delta(int32 orientation, float dx, float dy, float& outDx,
	float& outDy, int32 reflection = B_PANEL_REFLECTION_NONE)
{
#if VOS_DELTA_MAP_REVERSED
	// Only the 90-degree cases swap.
	int32 mapOrientation;
	switch (orientation) {
		case B_PANEL_ORIENTATION_LEFT_UP:
			mapOrientation = B_PANEL_ORIENTATION_RIGHT_UP;
			break;
		case B_PANEL_ORIENTATION_RIGHT_UP:
			mapOrientation = B_PANEL_ORIENTATION_LEFT_UP;
			break;
		default:
			mapOrientation = orientation;
			break;
	}
#else
	int32 mapOrientation = orientation;
#endif
	float rdx, rdy;
	switch (mapOrientation) {
		case B_PANEL_ORIENTATION_UPSIDE_DOWN:
			rdx = -dx;
			rdy = -dy;
			break;
		case B_PANEL_ORIENTATION_LEFT_UP:
			rdx = -dy;
			rdy = dx;
			break;
		case B_PANEL_ORIENTATION_RIGHT_UP:
			rdx = dy;
			rdy = -dx;
			break;
		default:
			rdx = dx;
			rdy = dy;
			break;
	}
	outDx = (reflection & B_PANEL_REFLECTION_X) != 0 ? -rdx : rdx;
	outDy = (reflection & B_PANEL_REFLECTION_Y) != 0 ? -rdy : rdy;
}


// Inverse of rotate_panel_point(), normalized logical to physical space.
// Reflection self-inverts, so it applies first here.
static inline void
unrotate_panel_point(int32 orientation, int32 reflection, float u,
	float v, float& outU, float& outV)
{
	float ru = (reflection & B_PANEL_REFLECTION_X) != 0 ? 1.0f - u : u;
	float rv = (reflection & B_PANEL_REFLECTION_Y) != 0 ? 1.0f - v : v;
	switch (orientation) {
		case B_PANEL_ORIENTATION_UPSIDE_DOWN:
			outU = 1.0f - ru;
			outV = 1.0f - rv;
			break;
		case B_PANEL_ORIENTATION_LEFT_UP:
			// rotate_panel_point(): ru = 1-v, rv = u
			outU = rv;
			outV = 1.0f - ru;
			break;
		case B_PANEL_ORIENTATION_RIGHT_UP:
			// rotate_panel_point(): ru = v, rv = 1-u
			outU = 1.0f - rv;
			outV = ru;
			break;
		default:
			outU = ru;
			outV = rv;
			break;
	}
}


// Maps a logical point through an orientation change, old logical to
// physical to new logical, so the pointer keeps its physical spot.
static inline void
remap_panel_point(int32 oldOrientation, int32 oldReflection,
	int32 newOrientation, int32 newReflection, float u, float v,
	float& outU, float& outV)
{
	float pu, pv;
	unrotate_panel_point(oldOrientation, oldReflection, u, v, pu, pv);
	rotate_panel_point(newOrientation, pu, pv, outU, outV, newReflection);
}


// Maps a raw 0..1 point in physical panel space onto the logical screen.
// The mouse add-on rotates before delivery and must not come through here.
static inline void
map_device_absolute_point(int32 orientation, int32 reflection, float u,
	float v, float width, float height, float& outX, float& outY)
{
	float lu, lv;
	rotate_panel_point(orientation, u, v, lu, lv, reflection);
	outX = lu * width;
	outY = lv * height;
}


#endif	// PANEL_ORIENTATION_TRANSFORM_H
