/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// One mixer column: fader, mute, balance, peak meter and the node's own
// controls through the parameter web. Used by the Media preflet's Mixer
// section for devices and application streams alike.

#ifndef CHANNEL_STRIP_VIEW_H
#define CHANNEL_STRIP_VIEW_H


#include <GroupView.h>
#include <String.h>

#include <media2/Controllable.h>


class BSlider;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BView;
class LevelMeterView;
class BParameter;


// The default theme wraps its view in a scroller; callers get the bare view.
BView*	UnwrapThemeView(BView* themed);


class ChannelStripView : public BGroupView {
public:
							ChannelStripView(uint32 nodeId, bool isDevice,
								bool isOutput, const char* name,
								bool showRoute,
								const char* nodeName = NULL);
	virtual					~ChannelStripView();

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			MessageReceived(BMessage* message);

			uint32			NodeId() const { return fNodeId; }

			void			ExternalChangeHint();

private:
	enum {
		kMsgFader     = 'CSTf',
		kMsgMute      = 'CSTm',
		kMsgBalance   = 'CSTb',
		kMsgRoute     = 'CSTr',
		kMsgPeak      = 'PWPk'
	};

			void			_BindWeb();
			BParameter*		_ParamWithKind(const char* kind) const;
			void			_RefreshFromGraph();
			void			_RefreshFromWeb();
			void			_ApplyFader();
			void			_ApplyMute();
			void			_ApplyBalance();
			void			_PopulateRouteMenu();
			void			_AddThemeView();

			uint32			fNodeId;
			bool			fIsDevice;
			bool			fIsOutput;
			bool			fSuppress;
			bigtime_t		fLastLocalWrite;
			BString			fNodeName;

			BStringView*	fNameLabel;
			BSlider*		fFader;
			BCheckBox*		fMuteBox;
			BSlider*		fBalance;
			LevelMeterView*	fMeter;
			BMenuField*		fRouteField;
			BPopUpMenu*		fRouteMenu;
			BView*			fThemeView;

			BControllable	fControllable;
			BParameter*		fGainParam;
			BParameter*		fMuteParam;
			BParameter*		fBalanceParam;
};


#endif
