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


// Quarter turns an orientation applies, in rotate_panel_point()'s direction.
static inline int32
panel_orientation_turns(int32 orientation)
{
	switch (orientation) {
		case B_PANEL_ORIENTATION_LEFT_UP:
			return 1;
		case B_PANEL_ORIENTATION_UPSIDE_DOWN:
			return 2;
		case B_PANEL_ORIENTATION_RIGHT_UP:
			return 3;
		default:
			return 0;
	}
}


static inline int32
panel_orientation_from_turns(int32 turns)
{
	switch (((turns % 4) + 4) % 4) {
		case 1:
			return B_PANEL_ORIENTATION_LEFT_UP;
		case 2:
			return B_PANEL_ORIENTATION_UPSIDE_DOWN;
		case 3:
			return B_PANEL_ORIENTATION_RIGHT_UP;
		default:
			return B_PANEL_ORIENTATION_NORMAL;
	}
}


// The user's rotation is relative to how the panel is mounted, as on other
// desktops: "normal" is upright for the device as it is meant to be held.
// The scanout needs both combined.
static inline int32
panel_compose_orientation(int32 mounting, int32 user)
{
	return panel_orientation_from_turns(panel_orientation_turns(mounting)
		+ panel_orientation_turns(user));
}


// Inverse of panel_compose_orientation(): the user rotation that gives
// the absolute one on this mounting.
static inline int32
panel_relative_orientation(int32 absolute, int32 mounting)
{
	return panel_orientation_from_turns(panel_orientation_turns(absolute)
		- panel_orientation_turns(mounting));
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


// Logical to physical pixel index for present-time blits, the inverse of
// rotate_panel_point(). Every per-pixel present blit must use it.
// Reflection applies in logical space before the rotation.
static inline void
rotate_point(int32 orientation, int32 lx, int32 ly, int32 logicalW,
	int32 logicalH, int32& px, int32& py,
	int32 reflection = B_PANEL_REFLECTION_NONE)
{
	int32 rx = (reflection & B_PANEL_REFLECTION_X) != 0
		? logicalW - 1 - lx : lx;
	int32 ry = (reflection & B_PANEL_REFLECTION_Y) != 0
		? logicalH - 1 - ly : ly;

	switch (orientation) {
		case B_PANEL_ORIENTATION_UPSIDE_DOWN:
			px = logicalW - 1 - rx;
			py = logicalH - 1 - ry;
			break;
		case B_PANEL_ORIENTATION_LEFT_UP:
			px = ry;
			py = logicalW - 1 - rx;
			break;
		case B_PANEL_ORIENTATION_RIGHT_UP:
			px = logicalH - 1 - ry;
			py = rx;
			break;
		default:
			px = rx;
			py = ry;
			break;
	}
}


#endif	// PANEL_ORIENTATION_TRANSFORM_H
