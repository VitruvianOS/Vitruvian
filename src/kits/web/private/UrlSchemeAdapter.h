/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_URL_SCHEME_ADAPTER_H
#define VOS_URL_SCHEME_ADAPTER_H


#include <wpe/webkit.h>


// Directory-backed URI schemes: the app points a scheme (e.g.
// "vos-mail") at a directory of static files before any view is
// attached; every WebKitWebContext then serves that scheme from disk.
void	SetUriSchemeDirectory(const char* scheme, const char* path);


class UrlSchemeAdapter {
public:
					UrlSchemeAdapter();
					~UrlSchemeAdapter();

		void		RegisterSchemes(WebKitWebContext* context);

private:
	static void			_HandleVosError(WebKitURISchemeRequest* request,
						gpointer data);
	static void			_HandleVosAbout(WebKitURISchemeRequest* request,
						gpointer data);
	static void			_HandleDirectoryScheme(
						WebKitURISchemeRequest* request,
						gpointer data);

	static void		_FinishWithHTML(WebKitURISchemeRequest* request,
						const char* html);
	static void		_FinishWithError(WebKitURISchemeRequest* request);
	static const char* _MimeTypeForPath(const char* path);
};


#endif // VOS_URL_SCHEME_ADAPTER_H
