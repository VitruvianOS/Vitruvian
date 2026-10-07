/*
 * Copyright 2005, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Inspired by SoundCapture from Be newsletter (Media Kit Basics: Consumers and Producers)
 */
#ifndef RECORDERWINDOW_H
#define RECORDERWINDOW_H


#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <Locale.h>
#include <ObjectList.h>
#include <Window.h>

#include <media2/MediaDefs.h>
#include <media2/MediaFile.h>
#include <media2/MediaGraph.h>
#include <media2/MediaPlayer.h>
#include <media2/MediaRecorder.h>
#include <media2/MediaTrack.h>

#include "DrawButton.h"
#include "ScopeView.h"
#include "SoundListView.h"
#include "TransportButton.h"
#include "TrackSlider.h"
#include "UpDownButton.h"
#include "VolumeSlider.h"
#include "VUView.h"


class BBox;
class BButton;
class BCheckBox;
class BMenuField;
class SoundListView;
class BScrollView;
class BSlider;
class BStringView;


class RecorderWindow : public BWindow {
public:
		RecorderWindow();
	virtual	~RecorderWindow();
		status_t InitCheck();


	virtual	bool QuitRequested();
	virtual	void MessageReceived(BMessage * message);

		enum {
			RECORD = 'cw00',			//	command messages
			PLAY,
			STOP,
			REWIND,
			FORWARD,
			SAVE,
			VIEW_LIST,
			LOOP,
			INPUT_SELECTED = 'cW00',	//	control messages
			SOUND_SELECTED,
			STOP_PLAYING,
			STOP_RECORDING,
			RECORD_PERIOD,
			PLAY_PERIOD,
			UPDATE_TRACKSLIDER,
			POSITION_CHANGED
		};

		void AddSoundItem(const BEntry& entry, bool temp = false);

		void RemoveCurrentSoundItem();

private:
		VUView *fVUView;
		ScopeView *fScopeView;
		RecordButton * fRecordButton;
		PlayPauseButton * fPlayButton;
		TransportButton * fStopButton;
		TransportButton * fRewindButton;
		TransportButton * fForwardButton;
		TransportButton * fSaveButton;
		DrawButton * fLoopButton;
		VolumeSlider *fVolumeSlider;
		TrackSlider *fTrackSlider;
		UpDownButton * fUpDownButton;
		BMenuField * fInputField;
		BMediaRecorder * fRecorder;
		BMediaPlayer * fPlayer;
		bool fRecording;
		SoundListView * fSoundList;
		BDirectory fTempDir;
		int fTempCount;

		float fDeployedHeight;

		BBox * fBottomBox;
		BBox * fFileInfoBox;
		BStringView *fFilename;
		BStringView *fFormat;
		BStringView *fCompression;
		BStringView *fChannels;
		BStringView *fSampleSize;
		BStringView *fSampleRate;
		BStringView *fDuration;

		enum BtnState {
			btnPaused,
			btnRecording,
			btnPlaying
		};
		BtnState fButtonState;
		BEntry fRecEntry;

		BMediaFormat fRecordFormat;

		BFile fRecFile;
		off_t fRecSize;

		media_client_id fInputDeviceId;
		BMediaGraph::ForeignPort fConnectedSource;
		bool fHasConnectedSource;

		BMediaFile *fPlayFile;
		BMediaFormat fPlayFormat;
		BMediaTrack *fPlayTrack;
		int64 fPlayLimit;
		int64 fPlayFrame;
		int64 fPlayFrames;

		bool fLooping;

		BFilePanel *fSavePanel;
		status_t fInitCheck;

		status_t InitWindow();

		void Record(BMessage * message);
		void Play(BMessage * message);
		void Stop(BMessage * message);
		void Save(BMessage * message);
		void DoSave(BMessage * message);
		void Input(BMessage * message);
		void Length(BMessage * message);
		void Selected(BMessage * message);

		status_t MakeRecordConnection(media_client_id deviceId);
		status_t BreakRecordConnection();
		status_t StopRecording();

		status_t StopPlaying();
		void _UpdatePlayPosition();

		status_t NewTempName(char * buffer);
		void CalcSizes(float min_width, float min_height);
		void SetButtonState(BtnState state);
		void UpdateButtons();
		status_t UpdatePlayFile(SoundListItem *item, bool updateDisplay = false);
		void ErrorAlert(const char * action, status_t err);
		void _PopulateInputMenu();
		void _ShowPlayerMessage(BMessage* message);

static	void RecordFile(void * cookie, bigtime_t timestamp, void * data,
				size_t size, const media_raw_audio_format & format);
static	void NotifyRecordFile(void * cookie,
				BMediaRecorder::notification code, ...);

		void RefsReceived(BMessage *msg);
		void CopyTarget(BMessage *msg);
};

#endif	/*	RECORDERWINDOW_H */
