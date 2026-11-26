/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NETWORK_SHARES_H
#define _NETWORK_SHARES_H


#include <Application.h>


class NetworkShares : public BApplication {
public:
					NetworkShares();

	virtual	void	ReadyToRun();
	virtual	void	MessageReceived(BMessage* message);
};


#endif	// _NETWORK_SHARES_H
