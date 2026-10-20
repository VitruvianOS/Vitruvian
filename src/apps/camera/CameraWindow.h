/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _CAMERA_WINDOW_H
#define _CAMERA_WINDOW_H


#include <Bitmap.h>
#include <Locker.h>
#include <ObjectList.h>
#include <Path.h>
#include <String.h>
#include <Window.h>

#include <media2/CameraSource.h>


class AudioCapture;
class BButton;
class BMediaRecorder;
class BMessageRunner;
class BMenuField;
class BPopUpMenu;
class BSlider;
class BStringView;
class BMediaFilter;
class BVideoSplitter;
class CameraPreviewTap;
class CameraView;


class CameraWindow : public BWindow {
public:
								CameraWindow(BRect frame);
	virtual						~CameraWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				FrameResized(float width, float height);

			void				HandleFrame(const uint8* bits, size_t size,
									uint32 width, uint32 height);

private:
			void				_BuildGraph();
			void				_RebuildChain();
			void				_DestroyChain();
			status_t			_OpenCamera(int32 index);
			void				_SetMode(int32 mode);
			void				_SetFilter(const char* filterId);
			void				_FitToPicture();
			void				_RefreshCameraMenu();
			void				_RefreshResolutionMenu();
			void				_RefreshFilterMenu();
			void				_RefreshMicMenu();
			void				_ShutterPressed();
			void				_CapturePhoto();
			void				_StartRecording();
			void				_StopRecording();
			void				_PlayShutter();
			void				_LoadSettings();
			void				_StoreSettings();
			void				_FrameArrived(BMessage* message);
			status_t			_RunTimer();
	static	status_t			_TimerEntry(void* data);

			CameraView*			fView;
			BStringView*		fStatusLabel;
			BButton*			fShutterButton;
			BButton*			fThumbButton;
			BMenuField*			fCameraField;
			BMenuField*			fModeField;
			BMenuField*			fResolutionField;
			BMenuField*			fFilterField;
			BMenuField*			fMicField;
			BPopUpMenu*			fModeMenu;
			BPopUpMenu*			fCameraMenu;
			BPopUpMenu*			fResolutionMenu;
			BPopUpMenu*			fFilterMenu;
			BPopUpMenu*			fMicMenu;
			BSlider*			fZoomSlider;
			BSlider*			fBrightnessSlider;
			BSlider*			fContrastSlider;
			BSlider*			fSaturationSlider;

			BCameraSource*		fSource;
			BVideoSplitter*		fSplitter;
			BMediaFilter*	fFilter;
			BMediaRecorder*		fRecorder;
			AudioCapture*		fAudio;
			CameraPreviewTap*	fTap;

			BLocker				fFrameLock;
			BBitmap*			fLastFrame;

			BObjectList<camera_device_info, true>* fCameras;
			int32				fCameraIndex;
			BString				fCameraStableId;
			BString				fFilterId;
			media_client_id		fMicId;
			int32				fMode;
			int32				fTimerDelay;
			int32				fReopens;
			BMessageRunner*		fTicker;
			bool				fRecording;
			bool				fBurstCancel;
			bigtime_t			fRecordStart;
			uint32				fWidth;
			uint32				fHeight;
			float				fFrameRate;
			bool				fGrid;
			bool				fMirrorEnabled;
			BPath				fLastPath;
			volatile bool		fQuitRequested;
};


#endif // _CAMERA_WINDOW_H
