/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SECRET_SERVICE_BRIDGE_H
#define _SECRET_SERVICE_BRIDGE_H


#include <SupportDefs.h>


class SecretServiceBridge {
public:
								SecretServiceBridge();
								~SecretServiceBridge();

			status_t			Start();
			void				Stop();

			class Impl;

private:
			Impl*				fImpl;
};


#endif	/* _SECRET_SERVICE_BRIDGE_H */
