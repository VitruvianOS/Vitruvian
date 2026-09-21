/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef RENAME_PANEL_H
#define RENAME_PANEL_H


#include <Window.h>


class BButton;
class BTextControl;


class RenamePanel : public BWindow {
public:
								RenamePanel(BWindow* window,
									const char* title, const char* label,
									const char* initialValue);
	virtual						~RenamePanel();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			status_t			Go(BString& value);
				// on B_OK, value holds the (possibly unchanged) new text

private:
			void				Cancel();

	class						EscapeFilter;

			BButton*			fOkButton;
			BTextControl*		fTextControl;
			status_t			fReturnStatus;
			sem_id				fExitSemaphore;
			BWindow*			fWindow;
};


#endif // RENAME_PANEL_H
