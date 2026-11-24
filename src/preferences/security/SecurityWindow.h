/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SECURITY_WINDOW_H
#define _SECURITY_WINDOW_H


#include <Window.h>


class FirewallView;


class SecurityWindow : public BWindow {
public:
								SecurityWindow();

	virtual	bool				QuitRequested();

private:
			void				_InitWindow();

			FirewallView*		fFirewallView;
};


#endif // _SECURITY_WINDOW_H
