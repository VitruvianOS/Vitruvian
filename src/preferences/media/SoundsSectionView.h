/*
 * Copyright 2026, Vitruvian Project.
 * Sounds section — system event -> sound file assignment, absorbed from
 * the standalone Sounds preflet (src/preferences/sounds/HWindow) as a
 * Media preferences section. Reuses HEventList/HEventRow and
 * SoundFilePanel unchanged; only the BWindow shell became a BGroupView.
 * Distributed under the terms of the MIT License.
 */

#ifndef SOUNDS_SECTION_VIEW_H
#define SOUNDS_SECTION_VIEW_H


#include <GroupView.h>

#include <Entry.h>


class BButton;
class BFileGameSound;
class HEventList;
class SoundFilePanel;


class SoundsSectionView : public BGroupView {
public:
							SoundsSectionView();
	virtual					~SoundsSectionView();

	virtual	void			AttachedToWindow();
	virtual	void			Pulse();
	virtual	void			MessageReceived(BMessage* message);

private:
			void			_SetupMenuField();

			HEventList*		fEventList;
			SoundFilePanel*	fFilePanel;
			BButton*		fPlayButton;
			BFileGameSound*	fPlayer;
			entry_ref		fPathRef;
};


#endif
