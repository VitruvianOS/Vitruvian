/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_URL_SCHEME_ADAPTER_H
#define VOS_URL_SCHEME_ADAPTER_H


#include <wpe/webkit.h>


class UrlSchemeAdapter {
public:
						UrlSchemeAdapter();
						~UrlSchemeAdapter();

			void		RegisterSchemes(WebKitWebContext* context);

private:
	static void			_HandleVosPDF(WebKitURISchemeRequest* request,
							gpointer data);
	static void			_HandleVosError(WebKitURISchemeRequest* request,
							gpointer data);
	static void			_HandleVosAbout(WebKitURISchemeRequest* request,
							gpointer data);

	static void			_FinishWithHTML(WebKitURISchemeRequest* request,
							const char* html);
};


#endif // VOS_URL_SCHEME_ADAPTER_H
