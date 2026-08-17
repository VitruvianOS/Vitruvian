/*
 * Copyright 2026, Vitruvian Project.
 * Media preferences — main window with sidebar (Output / Input / Streams /
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
class BPopUpMenu;
class BListView;
class DeviceListView;
class StreamListView;

#define kMediaSettingsFile "Media_prefs_settings"

class MediaWindow : public BWindow {
public:
							MediaWindow(BRect frame, int32 initialSection = 0);
	virtual					~MediaWindow();

	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

#ifndef MEDIA_PREFS_ENABLE_HARDWARE
#	define MEDIA_PREFS_ENABLE_HARDWARE 0
#endif

private:
			enum Section {
				kOutputSection    = kMediaSectionOutput,
				kInputSection     = kMediaSectionInput,
				kStreamsSection   = kMediaSectionStreams,
				kHardwareSection  = kMediaSectionHardware,
				kSoundsSection    = kMediaSectionSounds
			};

			std::vector<int32>	fSidebarSections;
			int32			_RowForSection(int32 section) const;

			void			_BuildSidebar();
			void			_ShowSection(Section s);
			void			_OnPipeWireChanged(uint32 what, uint32 deviceId);
			void			_PopulateHardwareDeviceMenu();
			void			_PopulateHardwareProfiles();

			BListView*		fSidebar;
			BView*			fContentPane;
			BView*			fCurrentSection;

			Section			fShownSection;
			bool			fHasShownSection;

			DeviceListView*	fOutputView;
			DeviceListView*	fInputView;
			StreamListView*	fStreamsView;
			BView*			fHardwareView;
			BView*			fSoundsView;

			BStringView*	fStatusLabel;
			bool			fWatchingPipeWire;

			BPopUpMenu*		fHardwareDeviceMenu;
			BListView*		fHardwareProfileList;
			BStringView*	fHardwareStatus;
			media_client_id	fHardwareSelectedDevice;
			int32			fHardwareActiveProfileIndex;
			bigtime_t		fHardwareLastLocalWrite;

			std::vector<int32>	fHardwareProfileIndices;
};

#endif
