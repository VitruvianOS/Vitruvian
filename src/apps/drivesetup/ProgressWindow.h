/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef PROGRESS_WINDOW_H
#define PROGRESS_WINDOW_H


#include <Window.h>


class BOutlineListView;
class BStatusBar;


// Results report, not live progress: RunPlan() blocks until the helper exits.
class ProgressWindow : public BWindow {
public:
								ProgressWindow(BWindow* window,
									const BMessage& result);
	virtual						~ProgressWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			void				Go();

private:
			BStatusBar*			fStatusBar;
			BOutlineListView*	fOpsList;
			sem_id				fExitSemaphore;
			BWindow*			fWindow;
};


#endif // PROGRESS_WINDOW_H
