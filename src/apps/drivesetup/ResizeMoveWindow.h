/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef RESIZE_MOVE_WINDOW_H
#define RESIZE_MOVE_WINDOW_H


#include <Window.h>


class BButton;
class BSlider;
class BStringView;
class BTextControl;


// Modal resize/move dialog; the caller builds and executes the jobs itself.
class ResizeMoveWindow : public BWindow {
public:
								ResizeMoveWindow(BWindow* window,
									const char* title,
									off_t currentSizeMiB, off_t minSizeMiB,
									off_t maxSizeMiB, bool canResize,
									off_t currentStartMiB, off_t minStartMiB,
									off_t maxStartMiB, bool canMove,
									off_t usedMiB = -1);
	virtual						~ResizeMoveWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			// On B_OK, outs hold the chosen values (current if a half is disabled).
			status_t			Go(off_t& newSizeMiB, off_t& newStartMiB);

private:
			void				Cancel();
			void				_UpdateSizeTextControl();
			void				_UpdateStartTextControl();

	class						EscapeFilter;

			BSlider*			fSizeSlider;
			BTextControl*		fSizeTextControl;
			BSlider*			fStartSlider;
			BTextControl*		fStartTextControl;
			BButton*			fOkButton;

			off_t				fMinSizeMiB;
			off_t				fMaxSizeMiB;
			off_t				fMinStartMiB;
			off_t				fMaxStartMiB;
			off_t				fCurrentSizeMiB;
			off_t				fCurrentStartMiB;
			bool				fCanResize;
			bool				fCanMove;

			status_t			fReturnStatus;
			sem_id				fExitSemaphore;
			BWindow*			fWindow;
};


#endif // RESIZE_MOVE_WINDOW_H
