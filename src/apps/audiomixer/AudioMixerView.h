/*
 * Copyright 2026, Vitruvian Project.
 * AudioMixer deskbar replicant — master volume, mute, device switch.
 * Distributed under the terms of the MIT License.
 */

#ifndef AUDIO_MIXER_VIEW_H
#define AUDIO_MIXER_VIEW_H


#include <Bitmap.h>
#include <Message.h>
#include <MessageRunner.h>
#include <String.h>
#include <View.h>


class BPopUpMenu;


enum {
	kMsgVolumeChanged  = 'AMVC',
	kMsgVolumeFinal    = 'AMVF',
	kMsgToggleMute     = 'AMTM',
	kMsgSelectOutput   = 'AMSO',
	kMsgSelectInput    = 'AMSI',
	kMsgOpenMediaPrefs = 'AMOP',
	kMsgOpenSoundPrefs = 'AMSP',
	kMsgQuit           = 'AMQT',
	kMsgRefresh        = 'AMRF',
	kMsgGraphReady     = 'AMGR'
};


class AudioMixerView : public BView {
public:
							AudioMixerView(BRect frame, int32 resizingMode,
								bool inDeskbar = false);
							AudioMixerView(BMessage* archive);
	virtual					~AudioMixerView();

	static	AudioMixerView*	Instantiate(BMessage* archive);
	virtual	status_t		Archive(BMessage* archive, bool deep = true) const;

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			MouseDown(BPoint where);
	virtual	void			MouseUp(BPoint where);
	virtual	void			MouseMoved(BPoint where, uint32 transit,
								const BMessage* dragMessage);
	virtual	void			Draw(BRect updateRect);
	virtual	void			Pulse();
	virtual	void			FrameResized(float width, float height);

private:
			void			_Init();
			void			_LoadIcons();
			void			_UpdateState();
	static	status_t		_WarmupThreadEntry(void* data);
			void			_ShowVolumePopup(BPoint where);
			void			_ShowContextMenu(BPoint where);
			void			_LaunchBySig(const char* sig, const char* path);
			void			_OpenMixer();

			bool			fInDeskbar;
			bool			fMuted;
			float			fVolume;
			uint32			fDefaultSinkId;
			uint32			fDefaultSourceId;
			BString			fDefaultSinkName;
			BString			fDefaultSourceName;

			BBitmap*		fIcon;
			BBitmap*		fMutedIcon;
			BBitmap*		fNoDeviceIcon;
			BMessageRunner*	fRefreshRunner;
			BWindow*		fVolumePopup;
			bool			fDraggingInDeskbar;





			bool			fGraphReady;




			bool			fWarmupPending;
};


#endif
