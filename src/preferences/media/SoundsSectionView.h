/*
 * Copyright 2026, Vitruvian Project.
 * Sounds section: system event to sound file assignment.
 * Distributed under the terms of the MIT License.
 */

#ifndef SOUNDS_SECTION_VIEW_H
#define SOUNDS_SECTION_VIEW_H


#include <GroupView.h>

#include <Entry.h>


class BButton;
class BFileGameSound;
class BMessageFilter;
class HEventList;
class SoundFilePanel;


class SoundsSectionView : public BGroupView {
public:
							SoundsSectionView();
	virtual					~SoundsSectionView();

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			Pulse();
	virtual	void			MessageReceived(BMessage* message);

private:
			void			_SetupMenuField();
			void			_AssignDefaultSounds();

			HEventList*		fEventList;
			SoundFilePanel*	fFilePanel;
			BButton*		fPlayButton;
			BFileGameSound*	fPlayer;
			entry_ref		fPathRef;
			BMessageFilter*	fEventFilter;
};


#endif
