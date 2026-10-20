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
}


UrlSchemeAdapter::~UrlSchemeAdapter()
{
}


void
UrlSchemeAdapter::RegisterSchemes(WebKitWebContext* context)
{
	webkit_web_context_register_uri_scheme(context, "vos-error",
		_HandleVosError, this, nullptr);
	webkit_web_context_register_uri_scheme(context, "vos-about",
		_HandleVosAbout, this, nullptr);

	// Mark custom schemes as secure so mixed-content rules don't block them.
	WebKitSecurityManager* sm =
		webkit_web_context_get_security_manager(context);
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-error");
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-about");
	webkit_security_manager_register_uri_scheme_as_local(sm, "vos-about");

	log_info("UrlSchemeAdapter: registered vos-error, vos-about");
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
	// The search box reads NETPLUS_SEARCH_TEMPLATE, which NetPlus
	// injects as a user script so the page follows the engine from
	// the settings.
	_FinishWithHTML(request,
		"<!DOCTYPE html><html><head><meta charset='utf-8'><title>About NetPlus</title>"
		"<meta name='viewport' content='width=device-width'>"
		"</head>"
		"<body style='font-family:sans-serif;text-align:center;"
		"padding:3em'>"
		"<h1>NetPlus Browser</h1>"
		"<p>Version 0.5 \xc2\xa9 2026 V\\OS</p>"
		"<p>WPE WebKit on V\\OS</p>"
		"<form style='margin:2em auto;max-width:28em' "
		"onsubmit=\"event.preventDefault();"
		"var t = window.NETPLUS_SEARCH_TEMPLATE"
		" || 'https://duckduckgo.com/?q=%s';"
		"var q = encodeURIComponent(this.q.value);"
		"if (!q) return;"
		"location.href = t.indexOf('%s') >= 0"
		" ? t.replace('%s', q) : t + q;\">"
		"<input name='q' placeholder='Search the web' "
		"style='width:70%;padding:.4em'>"
		"<button style='padding:.4em .8em'>Search</button>"
		"</form>"
		"</body></html>");
}
