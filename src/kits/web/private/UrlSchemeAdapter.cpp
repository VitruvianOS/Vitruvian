/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "url_scheme_adapter.h"

#include "logging.h"

#include <cstdlib>
#include <cstring>
#include <wpe/webkit.h>


UrlSchemeAdapter::UrlSchemeAdapter()
{
	const char* home = getenv("HOME");
	fPdfjsPath = home
		? std::string(home) + "/.local/share/netplus/pdfjs"
		: "/usr/share/netplus/pdfjs";
}


UrlSchemeAdapter::~UrlSchemeAdapter()
{
}


void
UrlSchemeAdapter::RegisterSchemes(WebKitWebContext* context)
{
	webkit_web_context_register_uri_scheme(context, "vos-pdf",
		_HandleVosPDF, this, nullptr);
	webkit_web_context_register_uri_scheme(context, "vos-error",
		_HandleVosError, this, nullptr);
	webkit_web_context_register_uri_scheme(context, "vos-about",
		_HandleVosAbout, this, nullptr);

	// Mark custom schemes as secure so mixed-content rules don't block them.
	WebKitSecurityManager* sm =
		webkit_web_context_get_security_manager(context);
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-pdf");
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-error");
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-about");
	webkit_security_manager_register_uri_scheme_as_local(sm, "vos-pdf");
	webkit_security_manager_register_uri_scheme_as_local(sm, "vos-about");

	log_info("UrlSchemeAdapter: registered vos-pdf, vos-error, vos-about");
}


void
UrlSchemeAdapter::_FinishWithHTML(WebKitURISchemeRequest* request,
	const char* html)
{
	gsize len = (gsize)strlen(html);
	GInputStream* stream = g_memory_input_stream_new_from_data(
		g_memdup2(html, len), len, g_free);
	webkit_uri_scheme_request_finish(request, stream, (gint64)len, "text/html");
	g_object_unref(stream);
}


void
UrlSchemeAdapter::_HandleVosPDF(WebKitURISchemeRequest* request, gpointer)
{
	_FinishWithHTML(request,
		"<!DOCTYPE html><html><head><title>PDF Viewer</title></head>"
		"<body style='font-family:sans-serif;padding:2em'>"
		"<h1>PDF.js Loading\xe2\x80\xa6</h1>"
		"<p>PDF.js not yet bundled. Place pdfjs/ in the data directory.</p>"
		"</body></html>");
}


void
UrlSchemeAdapter::_HandleVosError(WebKitURISchemeRequest* request, gpointer)
{
	_FinishWithHTML(request,
		"<!DOCTYPE html><html><head><title>Error</title></head>"
		"<body style='font-family:sans-serif;text-align:center;padding:2em'>"
		"<h1 style='color:#c00'>Page Load Error</h1>"
		"<p>The page could not be loaded.</p></body></html>");
}


void
UrlSchemeAdapter::_HandleVosAbout(WebKitURISchemeRequest* request, gpointer)
{
	_FinishWithHTML(request,
		"<!DOCTYPE html><html><head><title>About Net+</title></head>"
		"<body style='font-family:sans-serif;text-align:center;padding:2em'>"
		"<h1>Net+ Browser</h1>"
		"<p>Version 0.1 \xc2\xa9 2026 VitruvianOS Project</p>"
		"<p>WPE WebKit on Vitruvian</p>"
		"</body></html>");
}
