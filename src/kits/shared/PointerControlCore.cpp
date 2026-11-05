/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PointerControlCore.h"


void
pointer_constraint_init(pointer_constraint* constraint, int32 flags,
	float left, float top, float right, float bottom, float lockX,
	float lockY)
{
	if (constraint == NULL)
		return;

	constraint->flags = flags;
	constraint->rectLeft = left;
	constraint->rectTop = top;
	constraint->rectRight = right;
	constraint->rectBottom = bottom;
	constraint->lockX = lockX;
	constraint->lockY = lockY;
}


bool
pointer_constraint_active(const pointer_constraint* constraint)
{
	return constraint != NULL
		&& (constraint->flags
			& (kPointerConstraintConfined | kPointerConstraintRelative))
			!= 0;
}


bool
pointer_constraint_clamp(const pointer_constraint* constraint, float* x,
	float* y)
{
	if (constraint == NULL || x == NULL || y == NULL)
		return false;

	if ((constraint->flags & kPointerConstraintConfined) == 0)
		return false;

	// An inverted rect confines to nothing; treat it as unconstrained
	// rather than teleporting the cursor to a corner.
	if (constraint->rectRight < constraint->rectLeft
		|| constraint->rectBottom < constraint->rectTop)
		return false;

	float clampedX = *x;
	float clampedY = *y;
	if (clampedX < constraint->rectLeft)
		clampedX = constraint->rectLeft;
	else if (clampedX > constraint->rectRight)
		clampedX = constraint->rectRight;
	if (clampedY < constraint->rectTop)
		clampedY = constraint->rectTop;
	else if (clampedY > constraint->rectBottom)
		clampedY = constraint->rectBottom;

	if (clampedX == *x && clampedY == *y)
		return false;

	*x = clampedX;
	*y = clampedY;
	return true;
}


void
pointer_constraint_move_relative(pointer_constraint* constraint, float dx,
	float dy, float* x, float* y)
{
	if (constraint == NULL || x == NULL || y == NULL)
		return;

	if ((constraint->flags & kPointerConstraintRelative) == 0)
		return;

	constraint->lockX += dx;
	constraint->lockY += dy;
	pointer_constraint_clamp(constraint, &constraint->lockX,
		&constraint->lockY);

	*x = constraint->lockX;
	*y = constraint->lockY;
}


void
input_grab_init(input_grab_state* state)
{
	if (state == NULL)
		return;

	state->pointerOwnerTeam = -1;
	state->pointerTargetToken = -1;
	state->pointerFlags = 0;
	state->keyboardOwnerTeam = -1;
	state->keyboardTargetToken = -1;
	state->keyboardFlags = 0;
	state->secureAttentionTeam = -1;
}


bool
input_grab_acquire_pointer(input_grab_state* state, int32 team,
	int32 token, uint32 flags)
{
	if (state == NULL || team < 0 || token < 0)
		return false;

	if (state->pointerOwnerTeam >= 0
		&& state->pointerOwnerTeam != team)
		return false;

	state->pointerOwnerTeam = team;
	state->pointerTargetToken = token;
	state->pointerFlags = flags;
	return true;
}


bool
input_grab_acquire_keyboard(input_grab_state* state, int32 team,
	int32 token, uint32 flags)
{
	if (state == NULL || team < 0 || token < 0)
		return false;

	if (state->keyboardOwnerTeam >= 0
		&& state->keyboardOwnerTeam != team)
		return false;

	state->keyboardOwnerTeam = team;
	state->keyboardTargetToken = token;
	state->keyboardFlags = flags;
	return true;
}


bool
input_grab_release_pointer(input_grab_state* state, int32 team)
{
	if (state == NULL || state->pointerOwnerTeam != team)
		return false;

	state->pointerOwnerTeam = -1;
	state->pointerTargetToken = -1;
	state->pointerFlags = 0;
	return true;
}


bool
input_grab_release_keyboard(input_grab_state* state, int32 team)
{
	if (state == NULL || state->keyboardOwnerTeam != team)
		return false;

	state->keyboardOwnerTeam = -1;
	state->keyboardTargetToken = -1;
	state->keyboardFlags = 0;
	return true;
}


bool
input_grab_pointer_held_by(const input_grab_state* state, int32 team)
{
	return state != NULL && state->pointerOwnerTeam == team;
}


bool
input_grab_keyboard_held_by(const input_grab_state* state, int32 team)
{
	return state != NULL && state->keyboardOwnerTeam == team;
}


void
input_grab_release_for_team(input_grab_state* state, int32 team)
{
	if (state == NULL || team < 0)
		return;

	if (state->pointerOwnerTeam == team)
		input_grab_release_pointer(state, team);
	if (state->keyboardOwnerTeam == team)
		input_grab_release_keyboard(state, team);
}


void
input_grab_set_secure_attention_team(input_grab_state* state, int32 team)
{
	if (state == NULL)
		return;

	state->secureAttentionTeam = team;
}


int32
input_grab_pointer_target(const input_grab_state* state)
{
	if (state == NULL || state->secureAttentionTeam >= 0)
		return -1;

	if (state->pointerOwnerTeam < 0)
		return -1;

	return state->pointerTargetToken;
}


int32
input_grab_keyboard_target(const input_grab_state* state)
{
	if (state == NULL || state->secureAttentionTeam >= 0)
		return -1;

	if (state->keyboardOwnerTeam < 0)
		return -1;

	return state->keyboardTargetToken;
}
