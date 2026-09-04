/*
 * Copyright 2026, Vitruvian. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include <Window.h>

class BButton;
class BTextView;
class BScrollView;


class MainWindow : public BWindow {
public:
								MainWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_RunPrivilegedCommand();

			BButton*			fRunButton;
			BTextView*			fOutputView;
};

#endif // MAIN_WINDOW_H
