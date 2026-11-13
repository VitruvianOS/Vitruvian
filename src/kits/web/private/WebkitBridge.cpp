/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */


#include "webkit_bridge.h"
#include "compositor_adapter.h"
#include "ContentFilterStore.h"
#include "domain_filter.h"
#include "download_handler.h"
#include "focus_and_cursor.h"
#include "frame_notifier.h"
#include "logging.h"
#include "notifications_bridge.h"
#include "process_manager.h"
#include "session_adapter.h"
#include "url_scheme_adapter.h"

#include <Cursor.h>
#include <DataIO.h>
#include <OS.h>
#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <wpe/webkit.h>
#include <wpe/wpe.h>
#include <wpe/fdo.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>


// The script world GM userscripts and their prelude run in. It shares the
// page's DOM but not its JavaScript globals, so pages cannot reach what is
// registered for it (the netplusGM message handler).
static const char* const kUserscriptWorld = "netplus-userscripts";


// Completes the binding type before the destructor tears the
// vector down; the GObject signal holds the raw pointer.
struct WebKitBridge::ScriptMessageBinding {
	std::function<void(const std::string&)>	callback;
	std::string								name;
};


//	#pragma mark - Constructor/Destructor


WebKitBridge::WebKitBridge(int width, int height, SessionAdapter* session,
	int autoplayPolicy)
	:
	backend(nullptr),
	downloadHandler(nullptr),
	webView(nullptr),
	context(nullptr),
	settings(nullptr),
	fValid(false),
	fCrashReported(false),
	fCachedProgress(0.0),
	fCachedIsLoading(false),
	fCachedCanGoBack(false),
	fCachedCanGoForward(false),
	fPendingSize(0),
	fResizeScheduled(false),
	fExtensionGeneration(0),
	fBlockedCount(0),
	fAlive(std::make_shared<std::atomic<bool> >(true)),
	fGMExtensions(NULL)
{
	log_info("WebKitBridge: creating (size %dx%d)", width, height);
	ProcessManager::Instance().InvokeOnGLibThreadSync([&]() {
		_Init(width, height, session, autoplayPolicy);
	});
}


void
WebKitBridge::_Init(int width, int height, SessionAdapter* session,
	int autoplayPolicy)
{

	// 1. Compositor + WPE backend (FDO SHM path).
	compositor = std::make_unique<CompositorAdapter>(width, height);
	if (!compositor->Backend()) {
		log_error("WebKitBridge: compositor failed to create FDO backend");
		return;
	}
	backend = compositor->Backend();
	// WebKit asks this handler before entering full screen and refuses if
	// none is set; the embedder answers later with did_enter/did_exit.
	wpe_view_backend_set_fullscreen_handler(backend, _OnBackendFullscreen,
		this);

	notifier = std::make_unique<FrameNotifier>();
	compositor->onFrameReady = [this]() { notifier->Notify(); };

	// 2. WebKit context + URI schemes.
	// Audio is PipeWire only, through openal: pipewiresink deadlocks
	// when two players share its connection. The web process inherits
	// the environment; the sandbox needs the socket.
	setenv("GST_PLUGIN_FEATURE_RANK",
		"openalsink:MAX,pipewiresink:NONE,alsasink:NONE,pulsesink:NONE", 0);
	setenv("ALSOFT_DRIVERS", "pipewire", 0);
	// A paused off-screen video never resumed after a scroll or resize.
	setenv("WEBKIT_GST_ALLOW_PLAYBACK_OF_INVISIBLE_VIDEOS", "1", 0);
	context = webkit_web_context_new();
	if (const char* runtimeDir = g_getenv("XDG_RUNTIME_DIR")) {
		std::string socket = std::string(runtimeDir) + "/pipewire-0";
		webkit_web_context_add_path_to_sandbox(context, socket.c_str(),
			FALSE);
	}

	schemeAdapter = std::make_unique<UrlSchemeAdapter>();
	schemeAdapter->RegisterSchemes(context);

	// 3. Settings.
	settings = webkit_settings_new();
	webkit_settings_set_enable_javascript(settings, TRUE);
	webkit_settings_set_auto_load_images(settings, TRUE);
	webkit_settings_set_enable_fullscreen(settings, TRUE);
	webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
	webkit_settings_set_user_agent(settings,
		"Mozilla/5.0 (Linux; X11) "
		"AppleWebKit/605.1.15 (KHTML, like Gecko) "
		"Version/17.4 Safari/605.1.15 NetPlus/0.5");

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

	// The default policy only lets muted media start on its own, and WebKit
	// pauses un-muted video that leaves the viewport; a resize or full-screen
	// reflow did that and playback then needed a click to resume. The
	// policy is fixed at creation, so the user's choice is applied here.
	WebKitAutoplayPolicy autoplay = WEBKIT_AUTOPLAY_ALLOW;
	if (autoplayPolicy == 1)
		autoplay = WEBKIT_AUTOPLAY_ALLOW_WITHOUT_SOUND;
	else if (autoplayPolicy == 2)
		autoplay = WEBKIT_AUTOPLAY_DENY;
	WebKitWebsitePolicies* policies = webkit_website_policies_new_with_policies(
		"autoplay", autoplay, nullptr);

	webView = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
		"backend",          wkBackend,
		"web-context",      context,
		"network-session",  networkSession,
		"settings",         settings,
		"website-policies", policies,
		nullptr));
	g_object_unref(policies);
	// wkBackend is now owned by the WebView (transfer-full via "backend" prop).

	if (!webView) {
		log_error("WebKitBridge: failed to create WebKitWebView");
		// wkBackend ownership was supposed to transfer; if construction failed
		// it may still be live. WebKit destroys it on cleanup either way.
		return;
	}

	// 6. Downloads: one handler per session, this bridge listens.
	downloadHandler = DownloadHandler::ForSession(networkSession);
	{
		DownloadHandler::Listener listener;
		listener.onStarted = [this](const DownloadEntry& e) {
			if (onDownloadStarted) onDownloadStarted(e);
		};
		listener.onProgress = [this](const DownloadEntry& e) {
			if (onDownloadProgress) onDownloadProgress(e);
		};
		listener.onFinished = [this](const DownloadEntry& e) {
			if (onDownloadFinished) onDownloadFinished(e);
		};
		listener.onFailed = [this](const DownloadEntry& e) {
			if (onDownloadFailed) onDownloadFailed(e);
		};
		downloadHandler->AddListener(this, listener);
	}

	// 7. Focus adapter.
	focusAdapter = std::make_unique<FocusAndCursorAdapter>();
	focusAdapter->SetBackend(backend);

	// 8. Web notifications.
	notifications = std::make_unique<NotificationsBridge>();

	// 9. Extensions: the filter cache lives in the shared profile.
	if (session != NULL && !session->ProfileDir().empty())
		ContentFilterStore::Instance().SetCacheDir(
			session->ProfileDir() + "/content-filters");
	{
		WebKitUserContentManager* ucm
			= webkit_web_view_get_user_content_manager(webView);
		// GM.* calls answer synchronously through the reply API. The
		// handler belongs to the userscript world only: registered for
		// the page world (nullptr), any site's own scripts could call it
		// and read local files and intranet pages through it.
		webkit_user_content_manager_register_script_message_handler_with_reply(
			ucm, "netplusGM", kUserscriptWorld);
		g_signal_connect(ucm, "script-message-with-reply-received::netplusGM",
			G_CALLBACK(_OnGMMessage), this);
	}

	// 10. Initial size dispatch.
	struct SizeArgs { wpe_view_backend* b; uint32_t w, h; };
	auto* sa = new SizeArgs{backend, (uint32_t)width, (uint32_t)height};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<SizeArgs*>(d);
		wpe_view_backend_dispatch_set_size(a->b, a->w, a->h);
		delete a;
		return G_SOURCE_REMOVE;
	}, sa);

	// 11. Connect GObject signals.
	_ConnectSignals();

	fValid = true;
	log_info("WebKitBridge: created successfully");
}


WebKitBridge::~WebKitBridge()
{
	log_info("WebKitBridge: destroying");
	fAlive->store(false);
	if (downloadHandler != nullptr)
		downloadHandler->RemoveListener(this);
	ProcessManager::Instance().InvokeOnGLibThreadSync([this]() {
		for (auto& entry : fPageScripts) {
			if (entry.second != nullptr) {
				webkit_user_content_manager_remove_script(
					webkit_web_view_get_user_content_manager(webView),
					entry.second);
				webkit_user_script_unref(entry.second);
			}
		}
		fPageScripts.clear();
		for (auto& entry : fPageStyles) {
			if (entry.second != nullptr)
				webkit_user_style_sheet_unref(entry.second);
		}
		fPageStyles.clear();
		for (auto& entry : fPageFilters) {
			if (entry.second != nullptr)
				webkit_user_content_filter_unref(entry.second);
		}
		fPageFilters.clear();
		if (webView)
			g_object_unref(webView);
		if (context)
			g_object_unref(context);
		if (settings)
			g_object_unref(settings);
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
	g_signal_connect(webView, "load-failed",
		G_CALLBACK(_OnLoadFailed), this);
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
	g_signal_connect(webView, "permission-request",
		G_CALLBACK(_OnPermissionRequest), this);
	g_signal_connect(webView, "mouse-target-changed",
		G_CALLBACK(_OnMouseTargetChanged), this);
	g_signal_connect(webView, "context-menu",
		G_CALLBACK(_OnContextMenu), this);
	g_signal_connect(webView, "web-process-crashed",
		G_CALLBACK(_OnWebProcessCrashed), this);
	g_signal_connect(webView, "web-process-terminated",
		G_CALLBACK(_OnWebProcessTerminated), this);
	g_signal_connect(webView, "enter-fullscreen",
		G_CALLBACK(_OnEnterFullscreen), this);
	g_signal_connect(webView, "leave-fullscreen",
		G_CALLBACK(_OnLeaveFullscreen), this);
	g_signal_connect(webView, "run-file-chooser",
		G_CALLBACK(_OnRunFileChooser), this);
	g_signal_connect(webView, "show-notification",
		G_CALLBACK(_OnShowNotification), this);
	// Content-blocked and failed subresources both land here; the
	// count is the page's "blocked or failed" tally.
	g_signal_connect(webView, "resource-load-failed",
		G_CALLBACK(_OnResourceLoadFailed), this);
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
			self->fCrashReported = false;
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
WebKitBridge::_OnLoadFailed(WebKitWebView* view, WebKitLoadEvent event,
	const gchar* failingURI, GError* error, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	if (event != WEBKIT_LOAD_FINISHED)
		return;

	std::string message = error != NULL ? error->message : "load failed";
	{
		std::lock_guard<std::mutex> lock(self->fStateMutex);
		self->fCachedIsLoading = false;
	}
	if (self->onLoadFailed)
		self->onLoadFailed(message);
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
			// mailto: is not something a view can display; surface it
			// through the new-window callback so the host can hand it
			// to a mail client (WebMail builds its compose URL from
			// it). Net+ has no mail client and drops the link.
			if (g_str_has_prefix(uri, "mailto:")) {
				if (self->onNewWindowRequested)
					self->onNewWindowRequested(uri);
				webkit_policy_decision_ignore(decision);
				return TRUE;
			}

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

		// Re-apply the extension set before the document loads: this
		// runs on the GLib thread, ahead of the provisional load.
		if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION
			&& uri != nullptr && self->onNavigationStarting != nullptr)
			self->onNavigationStarting(uri);

		// Popups and target=_blank open in a host window: WebKit never
		// creates the new view itself, so the decision is always ignored
		// and the host is asked to open the URL.
		if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
			if (self->onNewWindowRequested && uri)
				self->onNewWindowRequested(uri);
			webkit_policy_decision_ignore(decision);
			return TRUE;
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

		// No in-tree PDF viewer: anything WebKit cannot render,
		// PDFs included, becomes a download.
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

	// The handlers block the GLib thread until the host answered: WebKit
	// needs the result before the handler returns.
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


gboolean
WebKitBridge::_OnPermissionRequest(WebKitWebView* view,
	WebKitPermissionRequest* request, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);

	if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) {
		const char* uri = webkit_web_view_get_uri(view);
		bool allow = false;
		if (self->fNotificationPermissionHandler != nullptr)
			allow = self->fNotificationPermissionHandler(
				uri != NULL ? uri : "");
		if (allow)
			webkit_permission_request_allow(request);
		else
			webkit_permission_request_deny(request);
		return TRUE;
	}

	// Everything else (geolocation, media devices, ...) is denied: the
	// host has no UI for it yet.
	webkit_permission_request_deny(request);
	return TRUE;
}


void
WebKitBridge::_OnMouseTargetChanged(WebKitWebView* view,
	WebKitHitTestResult* hitTest, guint modifiers, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	if (hitTest == NULL)
		return;

	guint context = webkit_hit_test_result_get_context(hitTest);
	const char* link = webkit_hit_test_result_get_link_uri(hitTest);
	const char* image = webkit_hit_test_result_get_image_uri(hitTest);

	if (self->onHoverChanged)
		self->onHoverChanged(link != NULL ? link : "",
			image != NULL ? image : "");

	if (self->onCursorChanged) {
		// Resize cursors belong to the embedder's window chrome:
		// WebKit's hit test reports page targets only.
		int32 cursorId = B_CURSOR_ID_SYSTEM_DEFAULT;
		if ((context & WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE) != 0)
			cursorId = B_CURSOR_ID_I_BEAM;
		else if ((context & WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK) != 0)
			cursorId = B_CURSOR_ID_FOLLOW_LINK;
		else if ((context & WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR) != 0)
			cursorId = B_CURSOR_ID_SYSTEM_DEFAULT;
		self->onCursorChanged(cursorId);
	}
}


gboolean
WebKitBridge::_OnContextMenu(WebKitWebView*, WebKitContextMenu* menu,
	WebKitHitTestResult* hitTest, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);

	std::string link;
	std::string image;
	if (hitTest != NULL) {
		const char* linkUri = webkit_hit_test_result_get_link_uri(hitTest);
		const char* imageUri = webkit_hit_test_result_get_image_uri(hitTest);
		if (linkUri != NULL)
			link = linkUri;
		if (imageUri != NULL)
			image = imageUri;
	}

	// Suppress WebKit's own menu; the host shows a BPopUpMenu instead.
	if (self->onContextMenu)
		self->onContextMenu(link, image, 0, 0);
	return TRUE;
}


gboolean
WebKitBridge::_OnWebProcessCrashed(WebKitWebView*, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	log_error("WebKitBridge: web process crashed");
	if (self->fCrashReported)
		return TRUE;
	self->fCrashReported = true;
	{
		std::lock_guard<std::mutex> lock(self->fStateMutex);
		self->fCachedIsLoading = false;
	}
	if (self->onWebProcessCrashed)
		self->onWebProcessCrashed();
	return TRUE;
}


void
WebKitBridge::_OnWebProcessTerminated(WebKitWebView* view,
	WebKitWebProcessTerminationReason reason, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	log_error("WebKitBridge: web process terminated (reason %d)", (int)reason);
	if (self->fCrashReported)
		return;
	self->fCrashReported = true;
	{
		std::lock_guard<std::mutex> lock(self->fStateMutex);
		self->fCachedIsLoading = false;
	}
	if (self->onWebProcessCrashed)
		self->onWebProcessCrashed();
}


/* static */ bool
WebKitBridge::_OnBackendFullscreen(void* data, bool enable)
{
	auto* self = static_cast<WebKitBridge*>(data);
	if (self->onFullscreenChanged == nullptr)
		return false;
	self->onFullscreenChanged(enable);
	return true;
}


gboolean
WebKitBridge::_OnEnterFullscreen(WebKitWebView*, gpointer)
{
	// TRUE would veto the switch; the backend handler drives it.
	return FALSE;
}


gboolean
WebKitBridge::_OnLeaveFullscreen(WebKitWebView*, gpointer)
{
	return FALSE;
}


gboolean
WebKitBridge::_OnRunFileChooser(WebKitWebView*, WebKitFileChooserRequest* request,
	gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);

	// The host shows a BFilePanel on its window thread and answers
	// asynchronously; returning TRUE takes the default away.
	g_object_ref(request);
	bool handled = false;
	if (self->onRunFileChooser)
		handled = self->onRunFileChooser(request);
	if (!handled) {
		webkit_file_chooser_request_select_files(request, NULL);
		g_object_unref(request);
	}
	return TRUE;
}


gboolean
WebKitBridge::_OnShowNotification(WebKitWebView*, WebKitNotification* notification,
	gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	const char* title = webkit_notification_get_title(notification);
	const char* body = webkit_notification_get_body(notification);
	if (self->onShowNotification)
		self->onShowNotification(title != NULL ? title : "",
			body != NULL ? body : "");
	// Keep the notification displayed; close would need a click handler.
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
	struct Args {
		WebKitBridge* self;
		WebKitWebView* v;
		std::string js;
		bool atStart;
	};
	auto* a = new Args{this, webView, js, atDocStart};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		WebKitUserContentManager* ucm =
			webkit_web_view_get_user_content_manager(a->v);
		// Replace a previous copy of the same script: settings changes
		// re-add it and one copy is enough.
		auto it = a->self->fUserScripts.find(a->js);
		if (it != a->self->fUserScripts.end()) {
			webkit_user_content_manager_remove_script(ucm, it->second);
			webkit_user_script_unref(it->second);
			a->self->fUserScripts.erase(it);
		}
		WebKitUserScript* script = webkit_user_script_new(
			a->js.c_str(),
			WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
			a->atStart
				? WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START
				: WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
			nullptr, nullptr);
		webkit_user_content_manager_add_script(ucm, script);
		a->self->fUserScripts[a->js] = script;
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


static void
_OnScriptMessage(WebKitUserContentManager*, JSCValue* value, gpointer data)
{
	auto* binding = static_cast<WebKitBridge::ScriptMessageBinding*>(data);
	if (binding == nullptr || !binding->callback)
		return;
	gchar* text = jsc_value_to_string(value);
	binding->callback(text != nullptr ? text : "");
	g_free(text);
}


void
WebKitBridge::RegisterScriptMessageHandler(const std::string& name,
	std::function<void(const std::string&)> cb)
{
	if (!fValid || webView == nullptr || !cb)
		return;

	auto owned = std::make_unique<ScriptMessageBinding>();
	owned->callback = cb;
	owned->name = name;
	ScriptMessageBinding* binding = owned.get();
	fScriptBindings.push_back(std::move(owned));

	struct Args {
		WebKitWebView*			view;
		ScriptMessageBinding*	binding;
	};
	auto* a = new Args{webView, binding};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		WebKitUserContentManager* ucm =
			webkit_web_view_get_user_content_manager(a->view);
		std::string signal = "script-message-received::" + a->binding->name;
		g_signal_connect(ucm, signal.c_str(),
			G_CALLBACK(_OnScriptMessage), a->binding);
		webkit_user_content_manager_register_script_message_handler(
			ucm, a->binding->name.c_str(), nullptr);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


//	#pragma mark - Extensions


void
WebKitBridge::SetGMExtensions(BWebExtensions* extensions)
{
	fGMExtensions = extensions;
}


void
WebKitBridge::SetNavigationStartingHandler(
	std::function<void(const std::string&)> fn)
{
	onNavigationStarting = fn;
}


void
WebKitBridge::PreloadContentFilters(
	const std::vector<WebExtensionFilter>& filters)
{
	if (!fValid)
		return;
	for (const auto& filter : filters) {
		ContentFilterStore::Instance().Get(filter.id, filter.rulesPath,
			[](WebKitUserContentFilter*) {});
	}
}


void
WebKitBridge::SetPageExtensions(
	const std::vector<WebExtensionScript>& scripts,
	const std::vector<WebExtensionStyle>& styles,
	const std::vector<WebExtensionFilter>& filters)
{
	if (!fValid)
		return;
	// Always through the sync path: from the navigation handler we
	// are already on the GLib thread and the re-sync must finish
	// before the policy decision proceeds.
	ProcessManager::Instance().InvokeOnGLibThreadSync([&]() {
		_ApplyPageExtensions(scripts, styles, filters);
	});
}


void
WebKitBridge::_ApplyPageExtensions(
	const std::vector<WebExtensionScript>& scripts,
	const std::vector<WebExtensionStyle>& styles,
	const std::vector<WebExtensionFilter>& filters)
{
	uint64_t generation = ++fExtensionGeneration;
	fBlockedCount.store(0);
	if (onBlockedCountChanged != nullptr)
		onBlockedCountChanged(0);

	WebKitUserContentManager* ucm
		= webkit_web_view_get_user_content_manager(webView);

	std::map<std::string, WebKitUserScript*> newScripts;
	for (const auto& script : scripts) {
		std::string key = "ext:" + script.id;
		auto it = fPageScripts.find(key);
		if (it != fPageScripts.end()) {
			newScripts[key] = it->second;
			fPageScripts.erase(it);
			continue;
		}
		WebKitUserScriptInjectionTime when = script.atDocumentStart
			? WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START
			: WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END;
		// GM scripts run in their own world, the only one that can reach
		// netplusGM; the rest share the page world as before.
		WebKitUserScript* userScript = script.isolated
			? webkit_user_script_new_for_world(script.source.c_str(),
				WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, when, kUserscriptWorld,
				nullptr, nullptr)
			: webkit_user_script_new(script.source.c_str(),
				WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, when, nullptr, nullptr);
		webkit_user_content_manager_add_script(ucm, userScript);
		newScripts[key] = userScript;
	}
	for (auto& stale : fPageScripts) {
		if (stale.second != nullptr) {
			webkit_user_content_manager_remove_script(ucm,
				stale.second);
			webkit_user_script_unref(stale.second);
		}
	}
	fPageScripts.swap(newScripts);

	std::map<std::string, WebKitUserStyleSheet*> newStyles;
	for (const auto& style : styles) {
		std::string key = "ext:" + style.id;
		auto it = fPageStyles.find(key);
		if (it != fPageStyles.end()) {
			newStyles[key] = it->second;
			fPageStyles.erase(it);
			continue;
		}
		WebKitUserStyleSheet* sheet = webkit_user_style_sheet_new(
			style.css.c_str(),
			WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
			WEBKIT_USER_STYLE_LEVEL_USER,
			nullptr, nullptr);
		webkit_user_content_manager_add_style_sheet(ucm, sheet);
		newStyles[key] = sheet;
	}
	for (auto& stale : fPageStyles) {
		if (stale.second != nullptr) {
			webkit_user_content_manager_remove_style_sheet(ucm,
				stale.second);
			webkit_user_style_sheet_unref(stale.second);
		}
	}
	fPageStyles.swap(newStyles);

	// Filters: the attach set is rebuilt in full whenever it changes
	// (remove_all_filters is the only removal the API offers).
	std::map<std::string, std::string> wanted;
	for (const auto& filter : filters)
		wanted[filter.id] = filter.rulesPath;
	bool changed = wanted.size() != fPageFilters.size();
	if (!changed) {
		for (const auto& entry : wanted) {
			if (fPageFilters.find(entry.first) == fPageFilters.end()) {
				changed = true;
				break;
			}
		}
	}
	if (!changed)
		return;
	for (auto& stale : fPageFilters) {
		if (stale.second != nullptr)
			webkit_user_content_filter_unref(stale.second);
	}
	fPageFilters.clear();
	webkit_user_content_manager_remove_all_filters(ucm);
	for (const auto& entry : wanted) {
		std::string id = entry.first;
		ContentFilterStore::Instance().Get(entry.first, entry.second,
			[this, generation, id](WebKitUserContentFilter* filter) {
				if (filter == nullptr || !fAlive->load()
					|| generation != fExtensionGeneration.load())
					return;
				WebKitUserContentManager* manager
					= webkit_web_view_get_user_content_manager(webView);
				webkit_user_content_manager_add_filter(manager, filter);
				webkit_user_content_filter_ref(filter);
				fPageFilters[id] = filter;
			});
	}
}


void
WebKitBridge::_OnResourceLoadFailed(WebKitWebView*, WebKitWebResource*,
	GError*, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	int32_t count = ++self->fBlockedCount;
	if (self->onBlockedCountChanged != nullptr)
		self->onBlockedCountChanged(count);
}


//	#pragma mark - GM.*


using namespace BPrivate::Network;


// Synchronous single-shot fetch for GM.xmlHttpRequest; runs on the
// GLib thread and blocks it for the duration, like script dialogs.
class _GMFetchListener : public BUrlProtocolListener {
public:
								_GMFetchListener()
									:
									fStatus(0),
									fSuccess(false)
								{
									fDone = create_sem(0, "netplus-gm-xhr");
								}

								~_GMFetchListener()
								{
									delete_sem(fDone);
								}

	virtual	void				ResponseStarted(BUrlRequest* caller)
								{
									BHttpResult* result = dynamic_cast<
										BHttpResult*>(const_cast<BUrlResult*>(
										&caller->Result()));
									if (result != NULL)
										fStatus = result->StatusCode();
								}

	virtual	void				RequestCompleted(BUrlRequest*, bool success)
								{
									fSuccess = success;
									release_sem(fDone);
								}

			sem_id				fDone;
			int32				fStatus;
			bool				fSuccess;
};


static std::string
_GMEscapeJSON(const std::string& in)
{
	std::string out;
	for (char c : in) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ((unsigned char)c < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					out += buffer;
				} else
					out += c;
				break;
		}
	}
	return out;
}


// Minimal reader for the flat {key: "string-or-number"} GM payloads.
static std::string
_GMJsonString(const std::string& body, const char* key, size_t from = 0)
{
	std::string needle = std::string("\"") + key + "\"";
	size_t pos = body.find(needle, from);
	if (pos == std::string::npos)
		return std::string();
	pos = body.find(':', pos + needle.size());
	if (pos == std::string::npos)
		return std::string();
	pos++;
	while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'
		|| body[pos] == '\n' || body[pos] == '\r'))
		pos++;
	if (pos >= body.size())
		return std::string();
	if (body[pos] == '"') {
		std::string value;
		pos++;
		while (pos < body.size() && body[pos] != '"') {
			if (body[pos] == '\\' && pos + 1 < body.size()) {
				pos++;
				switch (body[pos]) {
					case 'n': value += '\n'; break;
					case 'r': value += '\r'; break;
					case 't': value += '\t'; break;
					default: value += body[pos]; break;
				}
				pos++;
				continue;
			}
			value += body[pos++];
		}
		return value;
	}
	size_t end = pos;
	while (end < body.size() && body[end] != ',' && body[end] != '}'
		&& body[end] != ' ' && body[end] != '\n' && body[end] != '\r')
		end++;
	return body.substr(pos, end - pos);
}


/*!	Whether an address is one a page must not reach through the GM bridge:
	loopback, unspecified, private, carrier-grade NAT, link-local, unique
	local or multicast. These are the user's own machine and network.
*/
static bool
_GMIsLocalAddress(const struct sockaddr* address)
{
	if (address->sa_family == AF_INET) {
		uint32_t a = ntohl(((const struct sockaddr_in*)address)
			->sin_addr.s_addr);
		return (a >> 24) == 0 || (a >> 24) == 10 || (a >> 24) == 127
			|| (a >> 20) == (172 << 4 | 1) /* 172.16.0.0/12 */
			|| (a >> 16) == (192 << 8 | 168)
			|| (a >> 16) == (169 << 8 | 254)
			|| (a >> 22) == (100 << 2 | 1) /* 100.64.0.0/10 */
			|| (a >> 28) >= 0xe; /* multicast, reserved */
	}
	if (address->sa_family == AF_INET6) {
		const struct in6_addr& a
			= ((const struct sockaddr_in6*)address)->sin6_addr;
		if (IN6_IS_ADDR_V4MAPPED(&a)) {
			struct sockaddr_in v4 = {};
			v4.sin_family = AF_INET;
			memcpy(&v4.sin_addr, &a.s6_addr[12], 4);
			return _GMIsLocalAddress((const struct sockaddr*)&v4);
		}
		return IN6_IS_ADDR_LOOPBACK(&a) || IN6_IS_ADDR_UNSPECIFIED(&a)
			|| IN6_IS_ADDR_LINKLOCAL(&a) || IN6_IS_ADDR_SITELOCAL(&a)
			|| IN6_IS_ADDR_MULTICAST(&a)
			|| (a.s6_addr[0] & 0xfe) == 0xfc; /* fc00::/7 */
	}
	return true;
}


/*!	The GM fetch only goes to the public web: http and https, to a host
	that resolves to no local address. Returns an error for the reply, or
	an empty string when the URL may be fetched. A name that resolves to a
	public address now and a local one later (DNS rebinding) is not
	covered; redirects are not followed for the same reason.
*/
static std::string
_GMCheckFetchTarget(const BUrl& url)
{
	BString scheme = url.Protocol();
	scheme.ToLower();
	if (scheme != "http" && scheme != "https")
		return "scheme not allowed";
	BString host = url.Host();
	if (host.IsEmpty())
		return "no host";

	struct addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* addresses = NULL;
	if (getaddrinfo(host.String(), NULL, &hints, &addresses) != 0
		|| addresses == NULL)
		return "host not found";
	bool local = false;
	for (struct addrinfo* entry = addresses; entry != NULL;
			entry = entry->ai_next) {
		if (_GMIsLocalAddress(entry->ai_addr)) {
			local = true;
			break;
		}
	}
	freeaddrinfo(addresses);
	return local ? "local address not allowed" : "";
}


static std::string
_GMPerformFetch(const std::string& body)
{
	std::string url = _GMJsonString(body, "url");
	// "method" names the GM call itself; the HTTP verb has its own key.
	std::string method = _GMJsonString(body, "httpMethod");
	std::string data = _GMJsonString(body, "data");
	if (url.empty())
		return "{\"ok\":false,\"error\":\"no url\"}";
	if (!data.empty())
		return "{\"ok\":false,\"error\":\"upload unsupported\"}";
	if (!method.empty() && method != "GET" && method != "HEAD")
		return "{\"ok\":false,\"error\":\"method unsupported\"}";

	BMallocIO output;
	BUrl requestUrl;
	requestUrl.SetUrlString(BString(url.c_str()), true);
	std::string refused = _GMCheckFetchTarget(requestUrl);
	if (!refused.empty())
		return "{\"ok\":false,\"error\":\"" + refused + "\"}";
	_GMFetchListener listener;
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(requestUrl,
		&output, &listener);
	if (request == NULL)
		return "{\"ok\":false,\"error\":\"unsupported url\"}";
	// A redirect could lead to a local address the check above refused.
	if (BHttpRequest* http = dynamic_cast<BHttpRequest*>(request))
		http->SetFollowLocation(false);

	if (request->Run() < B_OK) {
		delete request;
		return "{\"ok\":false,\"error\":\"start failed\"}";
	}
	// Ten seconds is the userscript-manager norm; a stuck fetch must
	// not wedge the UI thread for longer.
	status_t wait = acquire_sem_etc(listener.fDone, 1, B_TIMEOUT, 10000000);
	if (wait != B_OK) {
		request->Stop();
		delete request;
		return "{\"ok\":false,\"error\":\"timeout\"}";
	}
	std::string text(static_cast<const char*>(output.Buffer()),
		output.BufferLength());
	bool success = listener.fSuccess;
	int32 status = listener.fStatus;
	delete request;

	char header[128];
	snprintf(header, sizeof(header), "{\"ok\":%s,\"status\":%d,",
		success ? "true" : "false", status);
	return std::string(header) + "\"text\":\"" + _GMEscapeJSON(text)
		+ "\"}";
}


std::string
WebKitBridge::_HandleGMCall(const std::string& body)
{
	std::string method = _GMJsonString(body, "method");
	std::string ext = _GMJsonString(body, "ext");
	if (method.empty())
		return "{\"ok\":false,\"error\":\"no method\"}";

	// Only an installed, enabled GM userscript may use the bridge, and
	// only under its own id (which also names its storage directory).
	if (fGMExtensions == NULL || !fGMExtensions->AllowsGM(ext.c_str()))
		return "{\"ok\":false,\"error\":\"not allowed\"}";

	if (method == "xmlHttpRequest")
		return _GMPerformFetch(body);
	if (method == "getValue") {
		std::string key = _GMJsonString(body, "key");
		std::string value;
		if (!fGMExtensions->GetValue(ext.c_str(), key.c_str(), &value))
			return "{\"ok\":false,\"error\":\"missing\"}";
		return "{\"ok\":true,\"value\":\"" + _GMEscapeJSON(value) + "\"}";
	}
	if (method == "setValue") {
		std::string key = _GMJsonString(body, "key");
		std::string value = _GMJsonString(body, "value");
		fGMExtensions->SetValue(ext.c_str(), key.c_str(), value.c_str());
		return "{\"ok\":true}";
	}
	return "{\"ok\":false,\"error\":\"unknown method\"}";
}


gboolean
WebKitBridge::_OnGMMessage(WebKitUserContentManager*, JSCValue* value,
	WebKitScriptMessageReply* reply, gpointer data)
{
	auto* self = static_cast<WebKitBridge*>(data);
	gchar* text = jsc_value_to_string(value);
	std::string response = self->_HandleGMCall(
		text != nullptr ? text : "");
	g_free(text);

	JSCContext* context = jsc_value_get_context(value);
	JSCValue* result = jsc_value_new_string(context, response.c_str());
	webkit_script_message_reply_return_value(reply, result);
	g_object_unref(result);
	return TRUE;
}


void
WebKitBridge::Resize(int width, int height)
{
	fPendingSize.store(((uint64_t)(uint32_t)width << 32) | (uint32_t)height);
	if (fResizeScheduled.exchange(true))
		return;

	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* self = static_cast<WebKitBridge*>(d);
		// Clear before reading so a size posted meanwhile queues a new task.
		self->fResizeScheduled.store(false);
		uint64_t size = self->fPendingSize.load();
		int w = (int)(size >> 32);
		int h = (int)(size & 0xffffffffu);
		self->compositor->Resize(w, h);
		wpe_view_backend_dispatch_set_size(self->backend,
			(uint32_t)w, (uint32_t)h);
		// A completion lost across the size change would stall WebKit;
		// an extra one with no frame pending is ignored.
		self->compositor->ScheduleFrameComplete();
		return G_SOURCE_REMOVE;
	}, this);
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


void
WebKitBridge::SetJavaScriptEnabled(bool enabled)
{
	struct Args { WebKitSettings* s; bool enabled; };
	auto* a = new Args{settings, enabled};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_settings_set_enable_javascript(a->s, a->enabled);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::SetImagesEnabled(bool enabled)
{
	struct Args { WebKitSettings* s; bool enabled; };
	auto* a = new Args{settings, enabled};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_settings_set_auto_load_images(a->s, a->enabled);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::SetUserAgentSuffix(const std::string& suffix)
{
	struct Args { WebKitSettings* s; std::string suffix; };
	auto* a = new Args{settings, suffix};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		if (a->suffix.empty()) {
			webkit_settings_set_user_agent(a->s,
				"Mozilla/5.0 (Linux; X11) "
				"AppleWebKit/605.1.15 (KHTML, like Gecko) "
				"Version/17.4 Safari/605.1.15 NetPlus/0.5");
		} else {
			std::string ua =
				"Mozilla/5.0 (Linux; X11) "
				"AppleWebKit/605.1.15 (KHTML, like Gecko) "
				"Version/17.4 Safari/605.1.15 NetPlus/0.5 " + a->suffix;
			webkit_settings_set_user_agent(a->s, ua.c_str());
		}
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::SetZoomLevel(double zoom)
{
	struct Args { WebKitWebView* v; double zoom; };
	auto* a = new Args{webView, zoom};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_web_view_set_zoom_level(a->v, a->zoom);
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::Cut()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_execute_editing_command(
			static_cast<WebKitWebView*>(d), "Cut");
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::Copy()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_execute_editing_command(
			static_cast<WebKitWebView*>(d), "Copy");
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::Paste()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_execute_editing_command(
			static_cast<WebKitWebView*>(d), "Paste");
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::PasteText(const std::string& text)
{
	struct Args { WebKitWebView* v; std::string text; };
	auto* a = new Args{webView, text};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		webkit_web_view_execute_editing_command_with_argument(a->v,
			"InsertText", a->text.c_str());
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::GetSelectedText(std::function<void(const std::string&)> cb)
{
	EvaluateJS("String(window.getSelection())",
		[cb](const std::string& value, bool /*error*/) {
			if (cb)
				cb(value);
		});
}


void
WebKitBridge::SelectAll()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		webkit_web_view_execute_editing_command(
			static_cast<WebKitWebView*>(d), "SelectAll");
		return G_SOURCE_REMOVE;
	}, webView);
}


void
WebKitBridge::DownloadURI(const std::string& uri,
	const std::string& destination)
{
	struct Args {
		WebKitWebView* view;
		DownloadHandler* handler;
		std::string uri;
		std::string destination;
	};
	auto* a = new Args{webView, downloadHandler, uri, destination};
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		auto* a = static_cast<Args*>(d);
		WebKitNetworkSession* session
			= webkit_web_view_get_network_session(a->view);
		if (!a->destination.empty())
			a->handler->SetDestinationOverride(a->uri, a->destination);
		webkit_network_session_download_uri(session, a->uri.c_str());
		delete a;
		return G_SOURCE_REMOVE;
	}, a);
}


void
WebKitBridge::NotifyDidEnterFullscreen()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		wpe_view_backend_dispatch_did_enter_fullscreen(
			static_cast<wpe_view_backend*>(d));
		return G_SOURCE_REMOVE;
	}, backend);
}


void
WebKitBridge::NotifyDidExitFullscreen()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		wpe_view_backend_dispatch_did_exit_fullscreen(
			static_cast<wpe_view_backend*>(d));
		return G_SOURCE_REMOVE;
	}, backend);
}


void
WebKitBridge::RequestExitFullscreen()
{
	ProcessManager::Instance().InvokeOnGLibThread([](void* d) -> gboolean {
		wpe_view_backend_dispatch_request_exit_fullscreen(
			static_cast<wpe_view_backend*>(d));
		return G_SOURCE_REMOVE;
	}, backend);
}


void
WebKitBridge::SetNotificationPermissionHandler(
	std::function<bool(const std::string&)> fn)
{
	fNotificationPermissionHandler = std::move(fn);
}
