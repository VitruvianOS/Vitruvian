/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * Distributed under the terms of the LGPL License.
 */
#ifndef VOS_WEBKIT_BRIDGE_H
#define VOS_WEBKIT_BRIDGE_H


#include <glib.h>
#include <wpe/webkit.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct CompositorAdapter;
struct FrameNotifier;
class DownloadHandler;
class FocusAndCursorAdapter;
class SessionAdapter;
class UrlSchemeAdapter;


struct WebKitBridge {
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

			// Cached state — safe to call from any thread.
			bool				CanGoBack() const;
			bool				CanGoForward() const;
			std::string			CurrentURL() const;
			std::string			Title() const;
			double				LoadProgress() const;
			bool				IsLoading() const;

			void				Resize(int width, int height);
			void				HandleFocusChange(bool focused);
			void				SetBlockedDomains(
									const std::vector<std::string>& domains);

	struct	wpe_view_backend*	backend;

			std::unique_ptr<CompositorAdapter>		compositor;
			std::unique_ptr<FrameNotifier>			notifier;
			std::unique_ptr<UrlSchemeAdapter>		schemeAdapter;
			std::unique_ptr<DownloadHandler>		downloadHandler;
			std::unique_ptr<FocusAndCursorAdapter>	focusAdapter;

			WebKitWebView*		webView;
			WebKitWebContext*	context;

			std::function<void(const std::string&)>	onTitleChanged;
			std::function<void(const std::string&)>	onURLChanged;
			std::function<void(double)>				onProgressChanged;
			std::function<void()>					onLoadFinished;
			std::function<void(const std::string&)>	onLoadFailed;
			std::function<void(const std::string&)>	onAlert;
			std::function<bool(const std::string&)>	onConfirm;
			std::function<std::string(const std::string&,
				const std::string&)>				onPrompt;
			std::function<bool(const std::string&)>	onNewWindowRequested;

private:
			void				_Init(int width, int height,
								SessionAdapter* session);
			void				_ConnectSignals();
			void				_UpdateCachedNavState();

			bool				fValid;

	mutable	std::mutex			fStateMutex;
			std::string			fCachedURL;
			std::string			fCachedTitle;
			double				fCachedProgress;
			bool				fCachedIsLoading;
			bool				fCachedCanGoBack;
			bool				fCachedCanGoForward;

	mutable	std::mutex			fBlockedMutex;
			std::vector<std::string> fBlockedDomains;

	// GObject signal handlers
	static void		_OnLoadChanged(WebKitWebView*, WebKitLoadEvent, gpointer);
	static void		_OnNotifyTitle(WebKitWebView*, GParamSpec*, gpointer);
	static void		_OnNotifyProgress(WebKitWebView*, GParamSpec*, gpointer);
	static void		_OnNotifyURI(WebKitWebView*, GParamSpec*, gpointer);
	static gboolean	_OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision*,
						WebKitPolicyDecisionType, gpointer);
	static gboolean	_OnScriptDialog(WebKitWebView*, WebKitScriptDialog*,
						gpointer);
};


#endif // VOS_WEBKIT_BRIDGE_H
