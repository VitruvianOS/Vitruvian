/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _EXPAND_VOLUME_WINDOW_H
#define _EXPAND_VOLUME_WINDOW_H

#include <DiskDeviceDefs.h>
#include <Locker.h>
#include <String.h>
#include <Window.h>

#include "ExpandGeometry.h"


class BButton;
class BSlider;
class BStatusBar;
class BStringView;


// Message codes ('EXPV' space).
enum {
	MSG_PROBE_DONE		= 'EXP1',
	MSG_APPLY_DONE		= 'EXP2',
	MSG_APPLY_TIMEOUT	= 'EXP3',
	MSG_USE_ALL			= 'EXP4',
	MSG_APPLY			= 'EXP5',
	MSG_SIZE_SLIDER		= 'EXP6',
	MSG_SHOW_WINDOW		= 'EXP7',
	MSG_TICK			= 'EXP8',
};


// One-window TryNow popup. Probes the system partition on a worker
// thread, lets the user pick a new size, then applies through the
// privileged plan helper (also off the window thread).
class ExpandVolumeWindow : public BWindow {
public:
							ExpandVolumeWindow(bool onlyIfExpandable = false);
	virtual					~ExpandVolumeWindow();

	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

private:
			void			_BuildControls();
			void			_StartProbe();
			void			_ApplyProbeResult(BMessage* message);
			void			_SetStateProbing();
			void			_SetStateNoSpace(const BString& reason);
			void			_SetStateReady();
			void			_SetStateBusy();
			void			_SetStateDone(const BString& summary);
			void			_SetStateError(const BString& detail);
			void			_UpdateSizeLabels();
			void			_StartApply();
			void			_FinishApply(BMessage* message);
			void			_NotifySuccess();
			void			_WriteMarker();

			BStringView*	fVolumeView;
			BStringView*	fDeviceView;
			BStringView*	fSizeView;
			BStringView*	fUsedView;
			BStringView*	fClaimableView;
			BStringView*	fNoticeView;
			BSlider*		fSizeSlider;
			BStatusBar*		fProgress;
			BButton*		fUseAllButton;
			BButton*		fApplyButton;
			BButton*		fCloseButton;

			int				fState;
			bool			fOnlyIfExpandable;

			// Probe results, filled once from the probe thread's message.
			partition_id	fRootPartitionID;
			partition_id	fDeviceID;
			BString			fVolumeName;
			BString			fDevicePath;
			BString			fFileSystem;
			off_t			fCurrentSize;
			off_t			fMaxSize;
			off_t			fBlockSize;
			off_t			fUsedBytes;
			off_t			fTotalBytes;
			bool			fMounted;
			bool			fCanResizeWhileMounted;

			// Slider speaks MiB, the kit speaks bytes.
			off_t			fCurrentSizeMiB;
			off_t			fMaxSizeMiB;
			off_t			fPendingSizeMiB;
			off_t			fPendingApplySize;

			// Apply worker bookkeeping.
			BLocker			fWorkerLock;
			int32			fApplyGeneration;
			bool			fWorkerRunning;
			bigtime_t		fApplyStartedAt;
};


static inline off_t
BytesToMiB(off_t bytes)
{
	return bytes / (1024 * 1024);
}


static inline off_t
MiBToBytes(off_t mib)
{
	return mib * 1024 * 1024;
}


#endif	// _EXPAND_VOLUME_WINDOW_H
