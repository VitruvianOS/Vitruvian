/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _POINTER_CONTROL_CORE_H
#define _POINTER_CONTROL_CORE_H


// Pure pointer-constraint and grab-arbitration logic shared by
// input_server (device-side clamping, relative mode) and app_server
// (event routing). No interface-kit types so the host checks can link
// this with a plain compiler.


#include <SupportDefs.h>


// pointer_constraint flags
enum {
	kPointerConstraintConfined	= 1 << 0,
		// the cursor may not leave the rect
	kPointerConstraintRelative	= 1 << 1,
		// motion is delta-only; the cursor stays on the lock point
};


struct pointer_constraint {
	int32	flags;
	float	rectLeft;
	float	rectTop;
	float	rectRight;
	float	rectBottom;
	float	lockX;
	float	lockY;
};


void	pointer_constraint_init(pointer_constraint* constraint,
				int32 flags, float left, float top, float right,
				float bottom, float lockX, float lockY);

bool	pointer_constraint_active(const pointer_constraint* constraint);

// Confine-clamp (*x, *y) into the constraint rect. Returns true when
// the point moved.
bool	pointer_constraint_clamp(const pointer_constraint* constraint,
				float* x, float* y);

// Relative mode: advance the frozen lock point by (dx, dy), clamping
// to the rect when also confined. Stores the new lock point and writes
// the result. No-op unless the constraint is relative.
void	pointer_constraint_move_relative(pointer_constraint* constraint,
				float dx, float dy, float* x, float* y);


// Grab arbitration. One pointer grab and one keyboard grab per desktop;
// the secure-attention team (authentication dialogs) suppresses both.
enum {
	kPointerGrabConfined		= 1 << 0,
	kPointerGrabRelative			= 1 << 1,
	kKeyboardGrabOwnerEvents	= 1 << 0,
};


struct input_grab_state {
	int32	pointerOwnerTeam;
	int32	pointerTargetToken;
	uint32	pointerFlags;

	int32	keyboardOwnerTeam;
	int32	keyboardTargetToken;
	uint32	keyboardFlags;

	int32	secureAttentionTeam;
};


void	input_grab_init(input_grab_state* state);
bool	input_grab_acquire_pointer(input_grab_state* state, int32 team,
				int32 token, uint32 flags);
bool	input_grab_acquire_keyboard(input_grab_state* state, int32 team,
				int32 token, uint32 flags);
// Only the owning team may release. Returns false when not the owner.
bool	input_grab_release_pointer(input_grab_state* state, int32 team);
bool	input_grab_release_keyboard(input_grab_state* state, int32 team);
bool	input_grab_pointer_held_by(const input_grab_state* state,
				int32 team);
bool	input_grab_keyboard_held_by(const input_grab_state* state,
				int32 team);
void	input_grab_release_for_team(input_grab_state* state, int32 team);
void	input_grab_set_secure_attention_team(input_grab_state* state,
				int32 team);

// Routing decisions: the token that must receive the event, or -1 to
// use the normal focus/hover path. A live secure-attention team
// suppresses grabs; keyboard grabs never see VT switch chords because
// the keyboard add-on consumes them upstream.
int32	input_grab_pointer_target(const input_grab_state* state);
int32	input_grab_keyboard_target(const input_grab_state* state);


#endif // _POINTER_CONTROL_CORE_H
