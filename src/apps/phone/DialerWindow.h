/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _DIALER_WINDOW_H
#define _DIALER_WINDOW_H


#include <Window.h>

#include "DialerView.h"


class DialerWindow : public BWindow {
public:
								DialerWindow(const BRect& frame);

	virtual	bool				QuitRequested();

			void				AppendKeypadKey(char key);
			void				RefreshLines();
			const char*			SelectedLine();

private:
			DialerView*			fDialerView;
};


#endif // _DIALER_WINDOW_H
