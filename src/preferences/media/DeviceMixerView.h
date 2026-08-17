/*
 * Copyright 2026, Vitruvian Project.
 * DeviceMixerView — per-device panel with master slider, mute, balance,
 * port (jack) selector, level meter, and "Set Default" — used by the
 * Output and Input sections of the Media preferences.
 * Distributed under the terms of the MIT License.
 */

#ifndef DEVICE_MIXER_VIEW_H
#define DEVICE_MIXER_VIEW_H


#include <GroupView.h>
#include <String.h>

#include <vector>


class BSlider;
class BCheckBox;
class BButton;
class BStringView;
class BPopUpMenu;
class BMenuField;
class LevelMeterView;


class DeviceMixerView : public BGroupView {
public:
							DeviceMixerView(uint32 deviceId, bool isOutput);
	virtual					~DeviceMixerView();

		virtual	void			AttachedToWindow();
		virtual	void			DetachedFromWindow();
		virtual	void			MessageReceived(BMessage* message);
		virtual	void			Pulse();

			void			RefreshFromBackend();
			void			_PopulatePorts();
			void			SetActive(bool active);





			void			ExternalChangeHint();

			uint32			DeviceId() const { return fDeviceId; }
			bool			IsOutput() const { return fIsOutput; }

private:
		enum {
			kMsgVolume     = 'DMVv',
			kMsgBalance    = 'DMVb',
			kMsgMute       = 'DMVm',
			kMsgSetDefault = 'DMVd',
			kMsgPort       = 'DMVp',


			kMsgPeakUpdate = 'PWPk'
		};

			uint32			fDeviceId;
			bool			fIsOutput;
			bool			fIsActive;
			bigtime_t		fLastLocalWrite;

			BStringView*	fNameLabel;
			BSlider*		fMasterSlider;
			BCheckBox*		fMuteBox;
			BSlider*		fBalanceSlider;
			BButton*		fDefaultButton;
			BMenuField*		fPortField;
			BPopUpMenu*		fPortMenu;
			std::vector<int32> fPortIds;
			LevelMeterView*	fMeter;
			BStringView*	fFormatLabel;
};


#endif
