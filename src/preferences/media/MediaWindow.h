/*
 * Copyright 2026, Vitruvian Project.
 * Media preferences: main window with sidebar (Output / Input / Streams /
 * Hardware / Sounds) and content pane.
 * Distributed under the terms of the MIT License.
 */

#ifndef MEDIA_WINDOW_H
#define MEDIA_WINDOW_H

#include <Window.h>

#include <vector>

#include <MediaDefs.h>
#include <MediaGraph.h>

#include "MediaMessages.h"

class BListView;
class BView;
class BStringView;
class BMessageRunner;
class BCardLayout;
class DeviceListView;
class StreamListView;
class MixerSectionView;
class HardwareSectionView;

#define kMediaSettingsFile "Media_prefs_settings"

class MediaWindow : public BWindow {
public:
							MediaWindow(BRect frame, int32 initialSection = 0);
	virtual					~MediaWindow();

	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

private:
			enum Section {
				kOutputSection    = kMediaSectionOutput,
				kInputSection     = kMediaSectionInput,
				kStreamsSection   = kMediaSectionStreams,
				kHardwareSection  = kMediaSectionHardware,
				kSoundsSection    = kMediaSectionSounds,
				kMixerSection     = kMediaSectionMixer
			};

			std::vector<int32>	fSidebarSections;
			int32			_RowForSection(int32 section) const;

			void			_BuildSidebar();
			void			_ShowSection(Section s);
			void			_OnPipeWireChanged(uint32 what, uint32 deviceId);
			void			_TryStartWatching();

			BListView*		fSidebar;
			BView*			fContentPane;
			BCardLayout*	fCards;
			BView*			fCurrentSection;

			Section			fShownSection;
			bool			fHasShownSection;

			MixerSectionView*	fMixerView;
			DeviceListView*	fOutputView;
			DeviceListView*	fInputView;
			StreamListView*	fStreamsView;
			HardwareSectionView*	fHardwareView;
			BView*			fSoundsView;

			BStringView*	fStatusLabel;
			bool			fWatchingPipeWire;
			BMessageRunner*	fWatchRetryRunner;
};

#endif
