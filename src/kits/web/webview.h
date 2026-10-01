/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_WEBVIEW_H
#define VOS_WEBVIEW_H


#include <Bitmap.h>
#include <Message.h>
#include <View.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>


namespace Vos {


class Session;


void				Init();
void				Shutdown();


class WebView {
public:
								BWebView(BView* hostView,
									Session* session = nullptr);
								~WebView();

			void				LoadURL(const std::string& url);
			void				LoadHTML(const std::string& html,
									const std::string& baseURL = "");
			void				Reload(bool bypassCache = false);
			void				Stop();
			bool				CanGoBack() const;
			bool				CanGoForward() const;
			void				GoBack();
			void				GoForward();

	using JSCallback = std::function<void(const std::string& result,
		bool hadError)>;
			void				EvaluateJS(const std::string& script,
									JSCallback callback = nullptr);
			void				AddUserScript(const std::string& js,
									bool atDocumentStart = true);
			void				AddUserStyleSheet(const std::string& css);

			std::string			CurrentURL() const;
			std::string			Title() const;
			double				LoadProgress() const;
			bool				IsLoading() const;

			void				SetBlockedDomains(
									const std::vector<std::string>& domains);

			void				HandleKeyEvent(const BMessage* message);
			void				HandleMouseEvent(const BMessage* message);
			void				HandleResize(int width, int height);
			void				HandleFocusChange(bool focused);

			BBitmap*			AcquireFrontBitmap();
			void				ReleaseFrontBitmap();

			int					NotifyPipeFd() const;

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
			struct Impl;
			std::unique_ptr<Impl> fImpl;
};


class Session {
public:
								Session(const std::string& profileDir,
									bool ephemeral = false);
								~Session();

			void				ClearCookies();
			void				ClearCache();
			std::string			ProfileDir() const;
			bool				IsEphemeral() const;

private:
			struct Impl;
			std::unique_ptr<Impl> fImpl;
};


}	// namespace Vos


#endif // VOS_WEBVIEW_H
