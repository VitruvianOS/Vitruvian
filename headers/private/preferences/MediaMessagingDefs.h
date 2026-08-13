/*
 * Copyright 2026, Vitruvian Project.
 * Shared replicant <-> Media preferences protocol.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MEDIA_MESSAGING_DEFS_H
#define _MEDIA_MESSAGING_DEFS_H


// Media preferences' app signature, shared with anyone that launches or
// messages it (the AudioMixer replicant, Deskbar menu entries, ...).
const char* const kMediaAppSignature = "application/x-vnd.Haiku-Media";

// Sent by the AudioMixer replicant (or anyone else) to jump straight to a
// sidebar section — int32 "section", one of the kMediaSection* values below.
// Delivered either as BRoster::Launch()'s initial message (cold start) or
// via BMessenger to the already-running app (warm case).
const uint32 kMsgSelectSection = 'MSel';

// MediaWindow::Section values, mirrored here so callers outside the
// preferences app (the replicant) don't have to include MediaWindow.h and
// can't drift from it silently. MediaWindow.h's enum is defined in terms of
// these constants.
const int32 kMediaSectionOutput   = 0;
const int32 kMediaSectionInput    = 1;
const int32 kMediaSectionStreams  = 2;
const int32 kMediaSectionHardware = 3;
const int32 kMediaSectionSounds   = 4;

#endif	// _MEDIA_MESSAGING_DEFS_H
