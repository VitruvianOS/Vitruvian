/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */


#include "webkit_bridge.h"
#include "compositor_adapter.h"
#include "domain_filter.h"
#include "download_handler.h"
#include "focus_and_cursor.h"
#include "frame_notifier.h"
#include "logging.h"
#include "process_manager.h"
#include "session_adapter.h"
#include "url_scheme_adapter.h"

#include <wpe/webkit.h>
#include <wpe/wpe.h>
#include <wpe/fdo.h>

#include <cstring>
#include <memory>
#include <vector>


//	#pragma mark - Constructor/Destructor


WebKitBridge::WebKitBridge(int width, int height, SessionAdapter* session)
	:
	backend(nullptr),
	webView(nullptr),
	context(nullptr),
	fValid(false),
	fCachedProgress(0.0),
	fCachedIsLoading(false),
	fCachedCanGoBack(false),
	fCachedCanGoForward(false)
{
	log_info("WebKitBridge: creating (size %dx%d)", width, height);
	ProcessManager::Instance().InvokeOnGLibThreadSync([&]() {
		_Init(width, height, session);
	});
}


void
WebKitBridge::_Init(int width, int height, SessionAdapter* session)
{

	// 1. Compositor + WPE backend (FDO SHM path).
	compositor = std::make_unique<CompositorAdapter>(width, height);
	if (!compositor->Backend()) {
		log_error("WebKitBridge: compositor failed to create FDO backend");
		return;
	}
	backend = compositor->Backend();

	notifier = std::make_unique<FrameNotifier>();
	compositor->onFrameReady = [this]() { notifier->Notify(); };

	// 2. WebKit context + URI schemes.
	context = webkit_web_context_new();

	schemeAdapter = std::make_unique<UrlSchemeAdapter>();
	schemeAdapter->RegisterSchemes(context);

	// 3. Settings.
	WebKitSettings* settings = webkit_settings_new();
	webkit_settings_set_enable_javascript(settings, TRUE);
	webkit_settings_set_auto_load_images(settings, TRUE);
	webkit_settings_set_user_agent(settings,
		"Mozilla/5.0 (Linux; X11) "
		"AppleWebKit/605.1.15 (KHTML, like Gecko) "
		"Version/17.4 Safari/605.1.15 Net+/0.1");

	// 4. WebKitWebViewBackend wrapping our FDO wpe_view_backend.
	// WebKit owns the exportable from here and destroys it after it is done
	// with the backend; destroying it ourselves first crashed on quit.
	WebKitWebViewBackend* wkBackend = webkit_web_view_backend_new(backend,
		(GDestroyNotify)wpe_view_backend_exportable_fdo_destroy,
		compositor->ReleaseExportable());

	// 5. Create the WebView, wiring in context, session, settings, backend.
	WebKitNetworkSession* networkSession =
		session ? session->NetworkSession()
				: webkit_network_session_get_default();

	webView = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
		"backend",         wkBackend,
		"web-context",     context,
		"network-session", networkSession,
		"settings",        settings,
		nullptr));
	g_object_unref(settings);
	// wkBackend is now owned by the WebView (transfer-full via "backend" prop).

	if (!webView) {
		log_error("WebKitBridge: failed to create WebKitWebView");
		// wkBackend ownership was supposed to transfer; if construction failed
		// it may still be live. WebKit destroys it on cleanup either way.
		return;
	}

	// 6. Downloads.
	downloadHandler = std::make_unique<DownloadHandler>();
	downloadHandler->Attach(networkSession);

	// 7. Focus adapter.
	focusAdapter = std::make_unique<FocusAndCursorAdapter>();
	focusAdapter->SetBackend(backend);

	// 8. Initial size dispatch.
	struct SizeArgs { wpe_view_backend* b; uint32_t w, h; };
	auto* sa = new SizeArgs{backend, (uint32_t)width, (uint32_t)height};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<SizeArgs*>(d);
		wpe_view_backend_dispatch_set_size(a->b, a->w, a->h);
		delete a;
		return G_SOURCE_REMOVE;
	}, sa);

	// 9. Connect GObject signals.
	_ConnectSignals();

	fValid = true;
	log_info("WebKitBridge: created successfully");
}


WebKitBridge::~WebKitBridge()
{
	log_info("WebKitBridge: destroying");
	ProcessManager::Instance().InvokeOnGLibThreadSync([this]() {
		if (webView)
			g_object_unref(webView);
		if (context)
			g_object_unref(context);
		notifier.reset();
		compositor.reset();
	});
}


//	#pragma mark - Private: signal wiring


void
WebKitBridge::_ConnectSignals()
{
	g_signal_connect(webView, "load-changed",
		G_CALLBACK(_OnLoadChanged), this);
	g_signal_connect(webView, "notify::title",
		G_CALLBACK(_OnNotifyTitle), this);
	g_signal_connect(webView, "notify::estimated-load-progress",
		G_CALLBACK(_OnNotifyProgress), this);
	g_signal_connect(webView, "notify::uri",
		G_CALLBACK(_OnNotifyURI), this);
	g_signal_connect(webView, "decide-policy",
		G_CALLBACK(_OnDecidePolicy), this);
	g_signal_connect(webView, "script-dialog",
		G_CALLBACK(_OnScriptDialog), this);
}


void
WebKitBridge::_UpdateCachedNavState()
{
	// Called on the GLib thread.
	std::lock_guard<std::mutex> lock(fStateMutex);
	fCachedCanGoBack    = webkit_web_view_can_go_back(webView);
	fCachedCanGoForward = webkit_web_view_can_go_forward(webView);
	const char* uri = webkit_web_view_get_uri(webView);
	if (uri)
		fCachedURL = uri;
}


//	#pragma mark - GObject signal handlers (all called on GLib thread)


void
WebKitBridge::_OnLoadChanged(WebKitWebView* view, WebKitLoadEvent event,
	gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);

	switch (event) {
		case WEBKIT_LOAD_STARTED:
		{
			const char* uri = webkit_web_view_get_uri(view);
			std::string url = uri ? uri : "";
			{
				std::lock_guard<std::mutex> lock(self->fStateMutex);
				self->fCachedIsLoading = true;
				if (!url.empty()) self->fCachedURL = url;
			}
			if (self->onURLChanged && !url.empty())
				self->onURLChanged(url);
			break;
		}

		case WEBKIT_LOAD_COMMITTED:
			self->_UpdateCachedNavState();
			break;

		case WEBKIT_LOAD_FINISHED:
		{
			self->_UpdateCachedNavState();
			{
				std::lock_guard<std::mutex> lock(self->fStateMutex);
				self->fCachedIsLoading = false;
				self->fCachedProgress  = 1.0;
			}
			if (self->onLoadFinished)
				self->onLoadFinished();
			break;
		}

		default:
			break;
	}
}


void
WebKitBridge::_OnNotifyTitle(WebKitWebView* view, GParamSpec*, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	const char* title = webkit_web_view_get_title(view);
	std::string t = title ? title : "";
	{
		std::lock_guard<std::mutex> lock(self->fStateMutex);
		self->fCachedTitle = t;
	}
	if (self->onTitleChanged)
		self->onTitleChanged(t);
}


void
WebKitBridge::_OnNotifyProgress(WebKitWebView* view, GParamSpec*, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	double p = webkit_web_view_get_estimated_load_progress(view);
	{
		std::lock_guard<std::mutex> lock(self->fStateMutex);
		self->fCachedProgress = p;
	}
	if (self->onProgressChanged)
		self->onProgressChanged(p);
}


void
WebKitBridge::_OnNotifyURI(WebKitWebView* view, GParamSpec*, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	const char* uri = webkit_web_view_get_uri(view);
	std::string url = uri ? uri : "";
	{
		std::lock_guard<std::mutex> lock(self->fStateMutex);
		if (!url.empty()) self->fCachedURL = url;
	}
	if (self->onURLChanged && !url.empty())
		self->onURLChanged(url);
}


gboolean
WebKitBridge::_OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
	WebKitPolicyDecisionType type, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);

	if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION
		|| type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {

		WebKitNavigationPolicyDecision* navDecision =
			WEBKIT_NAVIGATION_POLICY_DECISION(decision);
		WebKitNavigationAction* action =
			webkit_navigation_policy_decision_get_navigation_action(navDecision);
		WebKitURIRequest* request =
			webkit_navigation_action_get_request(action);
		const char* uri = webkit_uri_request_get_uri(request);

		if (uri) {
			// Extract host from URI for domain blocking.
			GUri* parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, nullptr);
			if (parsed) {
				const char* host = g_uri_get_host(parsed);
				if (host) {
					std::lock_guard<std::mutex> lock(self->fBlockedMutex);
					if (IsBlockedDomain(host, self->fBlockedDomains)) {
						log_info("WebKitBridge: blocked %s", host);
						webkit_policy_decision_ignore(decision);
						g_uri_unref(parsed);
						return TRUE;
					}
				}
				g_uri_unref(parsed);
			}
		}

		// For NEW_WINDOW_ACTION, give the host a chance to handle/refuse.
		if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
			bool allow = true;
			if (self->onNewWindowRequested && uri)
				allow = self->onNewWindowRequested(uri);
			if (!allow) {
				webkit_policy_decision_ignore(decision);
				return TRUE;
			}
		}
	}

	// A response WebKit cannot show (archives, installers, binaries) or one
	// the server marks as an attachment becomes a download; "use" would try
	// to render it in the page and nothing would happen.
	if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
		WebKitResponsePolicyDecision* response
			= WEBKIT_RESPONSE_POLICY_DECISION(decision);
		WebKitURIResponse* uriResponse
			= webkit_response_policy_decision_get_response(response);
		const char* disposition = NULL;
		SoupMessageHeaders* headers
			= webkit_uri_response_get_http_headers(uriResponse);
		if (headers != NULL)
			disposition = soup_message_headers_get_one(headers,
				"Content-Disposition");
		bool attachment = disposition != NULL
			&& g_ascii_strncasecmp(disposition, "attachment", 10) == 0;
		if (attachment
			|| !webkit_response_policy_decision_is_mime_type_supported(
				response)) {
			webkit_policy_decision_download(decision);
			return TRUE;
		}
	}

	webkit_policy_decision_use(decision);
	return TRUE;
}


gboolean
WebKitBridge::_OnScriptDialog(WebKitWebView*, WebKitScriptDialog* dialog,
	gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	const char* msg = webkit_script_dialog_get_message(dialog);
	std::string message = msg ? msg : "";

	switch (webkit_script_dialog_get_dialog_type(dialog)) {
		case WEBKIT_SCRIPT_DIALOG_ALERT:
			if (self->onAlert) self->onAlert(message);
			break;

		case WEBKIT_SCRIPT_DIALOG_CONFIRM:
			if (self->onConfirm)
				webkit_script_dialog_confirm_set_confirmed(dialog,
					self->onConfirm(message));
			break;

		case WEBKIT_SCRIPT_DIALOG_PROMPT:
		{
			const char* def =
				webkit_script_dialog_prompt_get_default_text(dialog);
			if (self->onPrompt) {
				std::string result =
					self->onPrompt(message, def ? def : "");
				webkit_script_dialog_prompt_set_text(dialog,
					result.c_str());
			}
			break;
		}

		default:
			return FALSE;
	}
	return TRUE;
}


//	#pragma mark - Cached state getters


bool
WebKitBridge::CanGoBack() const
{
	std::lock_guard<std::mutex> lock(fStateMutex);
	return fCachedCanGoBack;
}

bool
WebKitBridge::CanGoForward() const
{
	std::lock_guard<std::mutex> lock(fStateMutex);
	return fCachedCanGoForward;
}

std::string
WebKitBridge::CurrentURL() const
{
	std::lock_guard<std::mutex> lock(fStateMutex);
	return fCachedURL;
}

std::string
WebKitBridge::Title() const
{
	std::lock_guard<std::mutex> lock(fStateMutex);
	return fCachedTitle;
}

double
WebKitBridge::LoadProgress() const
{
	std::lock_guard<std::mutex> lock(fStateMutex);
	return fCachedProgress;
}

bool
WebKitBridge::IsLoading() const
{
	std::lock_guard<std::mutex> lock(fStateMutex);
	return fCachedIsLoading;
}


//	#pragma mark - Mutating calls (marshalled to GLib thread)


void
WebKitBridge::LoadURL(const std::string& url)
{
	log_info("WebKitBridge::LoadURL(%s)", url.c_str());
	struct Args { WebKitWebView* v; std::string url; };
	auto* a = new Args{webView, url};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_web_view_load_uri(a->v, a->url.c_str());
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::LoadHTML(const std::string& html, const std::string& baseURL)
{
	struct Args { WebKitWebView* v; std::string html; std::string base; };
	auto* a = new Args{webView, html, baseURL};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_web_view_load_html(a->v, a->html.c_str(),
			a->base.empty() ? nullptr : a->base.c_str());
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::Reload(bool bypassCache)
{
	struct Args { WebKitWebView* v; bool bypass; };
	auto* a = new Args{webView, bypassCache};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		if (a->bypass)
			webkit_web_view_reload_bypass_cache(a->v);
		else
			webkit_web_view_reload(a->v);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::Stop()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_stop_loading(static_cast<WebKitWebView*>(d));
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::GoBack()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_go_back(static_cast<WebKitWebView*>(d));
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::GoForward()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_go_forward(static_cast<WebKitWebView*>(d));
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::EvaluateJS(const std::string& script,
	std::function<void(const std::string&, bool)> cb)
{
	struct Args {
		WebKitWebView* v;
		std::string script;
		std::function<void(const std::string&, bool)> cb;
	};
	auto* a = new Args{webView, script, cb};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_web_view_evaluate_javascript(a->v,
			a->script.c_str(), -1, nullptr, nullptr, nullptr,
			[](GObject* obj, GAsyncResult* res, gpointer cbData) {
				auto* cbArgs = static_cast<Args*>(cbData);
				GError* error = nullptr;
				JSCValue* val = webkit_web_view_evaluate_javascript_finish(
					WEBKIT_WEB_VIEW(obj), res, &error);
				if (error) {
					if (cbArgs->cb) cbArgs->cb("", true);
					g_error_free(error);
				} else if (val) {
					gchar* str = jsc_value_to_string(val);
					if (cbArgs->cb) cbArgs->cb(str ? str : "", false);
					g_free(str);
					g_object_unref(val);
				} else if (cbArgs->cb) {
					cbArgs->cb("", true);
				}
				delete cbArgs;
			}, a);
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::AddUserScript(const std::string& js, bool atDocStart)
{
	struct Args { WebKitWebView* v; std::string js; bool atStart; };
	auto* a = new Args{webView, js, atDocStart};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		WebKitUserContentManager* ucm =
			webkit_web_view_get_user_content_manager(a->v);
		WebKitUserScript* script = webkit_user_script_new(
			a->js.c_str(),
			WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
			a->atStart
				? WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START
				: WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
			nullptr, nullptr);
		webkit_user_content_manager_add_script(ucm, script);
		webkit_user_script_unref(script);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::AddUserStyleSheet(const std::string& css)
{
	struct Args { WebKitWebView* v; std::string css; };
	auto* a = new Args{webView, css};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		WebKitUserContentManager* ucm =
			webkit_web_view_get_user_content_manager(a->v);
		WebKitUserStyleSheet* sheet = webkit_user_style_sheet_new(
			a->css.c_str(),
			WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
			WEBKIT_USER_STYLE_LEVEL_USER,
			nullptr, nullptr);
		webkit_user_content_manager_add_style_sheet(ucm, sheet);
		webkit_user_style_sheet_unref(sheet);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::Resize(int width, int height)
{
	struct ResizeArgs { CompositorAdapter* c; wpe_view_backend* b; int w, h; };
	auto* ra = new ResizeArgs{compositor.get(), backend, width, height};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<ResizeArgs*>(d);
		a->c->Resize(a->w, a->h);
		wpe_view_backend_dispatch_set_size(a->b,
			(uint32_t)a->w, (uint32_t)a->h);
		delete a;
		return G_SOURCE_REMOVE;
	}, ra);
}


void
WebKitBridge::HandleFocusChange(bool focused)
{
	struct FocusArgs { FocusAndCursorAdapter* f; bool focused; };
	auto* fa = new FocusArgs{focusAdapter.get(), focused};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<FocusArgs*>(d);
		a->f->SetFocused(a->focused);
		delete a;
		return G_SOURCE_REMOVE;
	}, fa);
}


void
WebKitBridge::SetBlockedDomains(const std::vector<std::string>& domains)
{
	std::lock_guard<std::mutex> lock(fBlockedMutex);
	fBlockedDomains = domains;
}
