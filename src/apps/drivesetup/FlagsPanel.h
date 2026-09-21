/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef FLAGS_PANEL_H
#define FLAGS_PANEL_H


#include <Window.h>


class BButton;
class BCheckBox;
class BPartition;


class FlagsPanel : public BWindow {
public:
								FlagsPanel(BWindow* window,
									BPartition* partition);
	virtual						~FlagsPanel();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			status_t			Go(BMessage& flags);
				// on B_OK, flags has zero or more "flag" string entries

private:
			void				Cancel();

	class						EscapeFilter;

			BButton*			fOkButton;
			BCheckBox*			fEspCheckBox;
			BCheckBox*			fLegacyBootCheckBox;
			status_t			fReturnStatus;
			sem_id				fExitSemaphore;
			BWindow*			fWindow;
};


#endif // FLAGS_PANEL_H
