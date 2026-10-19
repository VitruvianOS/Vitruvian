/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_WEBKIT_BRIDGE_H
#define VOS_WEBKIT_BRIDGE_H


#include <glib.h>
#include <wpe/webkit.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct CompositorAdapter;
struct FrameNotifier;
struct DownloadEntry;
class DownloadHandler;
class FocusAndCursorAdapter;
class NotificationsBridge;
class SessionAdapter;
class UrlSchemeAdapter;


struct WebKitBridge {
	// Completes in the .cpp; public so the GObject trampoline can
	// name it.
	struct	ScriptMessageBinding;

								WebKitBridge(int width, int height,
									SessionAdapter* session = nullptr);
								~WebKitBridge();

			bool				IsValid() const { return fValid; }

			void				LoadURL(const std::string& url);
			void				LoadHTML(const std::string& html,
									const std::string& baseURL);
			void				Reload(bool bypassCache);
			void				Stop();
			void				GoBack();
			void				GoForward();
			void				EvaluateJS(const std::string& script,
									std::function<void(const std::string&, bool)> cb);
			void				AddUserScript(const std::string& js,
									bool atDocStart);
			void				AddUserStyleSheet(const std::string& css);
			void				RegisterScriptMessageHandler(
										const std::string& name,
										std::function<void(const std::string&)> cb);

			// Cached state — safe to call from any thread.
			bool				CanGoBack() const;
			bool				CanGoForward() const;
			std::string			CurrentURL() const;
			std::string			Title() const;
			double				LoadProgress() const;
			bool				IsLoading() const;

			// Coalesced: only the latest size reaches the GLib thread.
			void				Resize(int width, int height);
			void				HandleFocusChange(bool focused);
			void				SetBlockedDomains(
									const std::vector<std::string>& domains);

			// Settings applied from the profile preferences.
			void				SetJavaScriptEnabled(bool enabled);
			void				SetImagesEnabled(bool enabled);
			void				SetUserAgentSuffix(const std::string& suffix);
			void				SetZoomLevel(double zoom);

			// Editing commands routed through WebKit.
			void				Cut();
			void				Copy();
			void				Paste();

			// WPE WebKit has no system clipboard bridge: the text is
			// inserted at the caret instead of asking WebKit to paste.
			void				PasteText(const std::string& text);

			// Resolves with the page selection ("" on failure).
			void				GetSelectedText(
									std::function<void(const std::string&)> cb);

			void				SelectAll();

			// Downloads a URI through the network session; a non-empty
			// destination overrides the default Downloads directory.
			void				DownloadURI(const std::string& uri,
									const std::string& destination);

			// Full-screen video: called after the host window entered or
			// left full-screen, on the GLib thread.
			void				NotifyDidEnterFullscreen();
			void				NotifyDidExitFullscreen();

			// Resolves true when the user allowed notifications for this
			// permission request; runs on the GLib thread.
			void				SetNotificationPermissionHandler(
									std::function<bool(const std::string&)> fn);

	struct	wpe_view_backend*	backend;

			std::unique_ptr<CompositorAdapter>		compositor;
			std::unique_ptr<FrameNotifier>			notifier;
			std::unique_ptr<UrlSchemeAdapter>		schemeAdapter;
			// Shared per network session; not owned.
			DownloadHandler*						downloadHandler;
			std::unique_ptr<FocusAndCursorAdapter>	focusAdapter;
			std::unique_ptr<NotificationsBridge>	notifications;

			WebKitWebView*		webView;
			WebKitWebContext*	context;
			WebKitSettings*		settings;

			// All callbacks run on the GLib thread: post, never lock a
			// BWindow from here.
			std::function<void(const std::string&)>	onTitleChanged;
			std::function<void(const std::string&)>	onURLChanged;
			std::function<void(double)>				onProgressChanged;
			std::function<void()>					onLoadFinished;
			std::function<void(const std::string&)>	onLoadFailed;
			std::function<void(const std::string&)>	onAlert;
			std::function<bool(const std::string&)>	onConfirm;
			std::function<std::string(const std::string&,
				const std::string&)>				onPrompt;
			// The host opens the URL in its own window; WebKit never
			// creates the new view itself.
			std::function<void(const std::string&)>	onNewWindowRequested;
			// Hit-test driven: link URI ("" when not over a link), the
			// image URI under the pointer ("" when none).
			std::function<void(const std::string&,
				const std::string&)>				onHoverChanged;
			// Cursor shape id (B_CURSOR_ID_*) for the current hit target.
			std::function<void(int32)>				onCursorChanged;
			// Context menu request with hit URIs; runs on the GLib
			// thread, the receiver posts to its window thread.
			std::function<void(const std::string& linkUri,
				const std::string& imageUri, int32 x, int32 y)>	onContextMenu;
			std::function<void()>					onWebProcessCrashed;
			std::function<void(bool)>				onFullscreenChanged;
			// Script dialogs block the GLib thread until the host
			// answers; std primitives only on both sides.
			std::function<bool(WebKitFileChooserRequest*)>
									onRunFileChooser;
			std::function<void(const std::string& title,
				const std::string& body)>			onShowNotification;
			// Download fan-out from the shared per-session handler.
			std::function<void(const struct DownloadEntry&)>
									onDownloadStarted;
			std::function<void(const struct DownloadEntry&)>
									onDownloadProgress;
			std::function<void(const struct DownloadEntry&)>
									onDownloadFinished;
			std::function<void(const struct DownloadEntry&)>
									onDownloadFailed;

private:
			void				_Init(int width, int height,
								SessionAdapter* session);
			void				_ConnectSignals();
			void				_UpdateCachedNavState();

			bool				fValid;
			// crash and terminated can both fire for one crash: the
			// error page is loaded once, until the next navigation.
			bool				fCrashReported;

	mutable	std::mutex			fStateMutex;
			std::string			fCachedURL;
			std::string			fCachedTitle;
			double				fCachedProgress;
			bool				fCachedIsLoading;
			bool				fCachedCanGoBack;
			bool				fCachedCanGoForward;

			// Latest requested size (width << 32 | height) and whether a
			// GLib task is already queued to apply it.
			std::atomic<uint64_t>	fPendingSize;
			std::atomic<bool>	fResizeScheduled;

	mutable	std::mutex			fBlockedMutex;
			std::vector<std::string> fBlockedDomains;

	// Script message handler bindings; the user content manager
	// keeps the raw pointer, this owns the objects.
			std::vector<std::unique_ptr<ScriptMessageBinding> >	fScriptBindings;

			// GLib thread only: scripts in the content manager, keyed
			// by content so a re-add replaces instead of stacking.
			std::map<std::string, WebKitUserScript*> fUserScripts;

			std::function<bool(const std::string&)>
									fNotificationPermissionHandler;

	// GObject signal handlers
	static void		_OnLoadChanged(WebKitWebView*, WebKitLoadEvent, gpointer);
	static void		_OnNotifyTitle(WebKitWebView*, GParamSpec*, gpointer);
	static void		_OnNotifyProgress(WebKitWebView*, GParamSpec*, gpointer);
	static void		_OnNotifyURI(WebKitWebView*, GParamSpec*, gpointer);
	static gboolean	_OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision*,
						WebKitPolicyDecisionType, gpointer);
	static gboolean	_OnScriptDialog(WebKitWebView*, WebKitScriptDialog*,
						gpointer);
	static gboolean	_OnPermissionRequest(WebKitWebView*,
						WebKitPermissionRequest*, gpointer);
	static void		_OnMouseTargetChanged(WebKitWebView*,
						WebKitHitTestResult*, guint, gpointer);
	static gboolean	_OnContextMenu(WebKitWebView*, WebKitContextMenu*,
						WebKitHitTestResult*, gpointer);
	static gboolean	_OnWebProcessCrashed(WebKitWebView*, gpointer);
	static void		_OnWebProcessTerminated(WebKitWebView*,
						WebKitWebProcessTerminationReason, gpointer);
	static bool		_OnBackendFullscreen(void* data, bool enable);
	static gboolean	_OnEnterFullscreen(WebKitWebView*, gpointer);
	static gboolean	_OnLeaveFullscreen(WebKitWebView*, gpointer);
	static gboolean	_OnRunFileChooser(WebKitWebView*,
						WebKitFileChooserRequest*, gpointer);
	static gboolean	_OnShowNotification(WebKitWebView*, WebKitNotification*,
						gpointer);
	static void		_OnLoadFailed(WebKitWebView*, WebKitLoadEvent,
						const gchar*, GError*, gpointer);
};


#endif // VOS_WEBKIT_BRIDGE_H
