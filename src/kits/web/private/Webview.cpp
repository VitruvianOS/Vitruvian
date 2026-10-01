#include <vos/webview.h>

#include "event_adapter.h"
#include "logging.h"
#include "process_manager.h"
#include "session_adapter.h"
#include "webkit_bridge.h"

#include <Bitmap.h>
#include <View.h>

#include <vector>


namespace Vos {


struct WebView::Impl {
	std::unique_ptr<SessionAdapter>	fSessionAdapter;
	std::unique_ptr<WebKitBridge>	fBridge;
	std::unique_ptr<EventAdapter>	fEventAdapter;
	BView*							fHostView;
	int								fWidth;
	int								fHeight;
	bool							fFocused;
};


//	#pragma mark - Free Functions


void
Init()
{
	log_info("Vos::Init");
	// SHM export path needs no display init; EGL would need
	// wpe_fdo_initialize_for_egl_display() here.
	ProcessManager::Instance().StartGLibLoop();
}


void
Shutdown()
{
	log_info("Vos::Shutdown");
	ProcessManager::Instance().StopGLibLoop();
}


//	#pragma mark - WebView Constructor/Destructor


WebView::WebView(BView* hostView, Session* session)
	:
	fImpl(std::make_unique<Impl>())
{
	fImpl->fHostView = hostView;
	fImpl->fWidth = (int)hostView->Bounds().Width() + 1;
	fImpl->fHeight = (int)hostView->Bounds().Height() + 1;
	fImpl->fFocused = false;

	if (session)
		fImpl->fSessionAdapter = std::make_unique<SessionAdapter>(
			session->ProfileDir(), session->IsEphemeral());

	fImpl->fBridge = std::make_unique<WebKitBridge>(
		fImpl->fWidth, fImpl->fHeight, fImpl->fSessionAdapter.get());

	if (!fImpl->fBridge->IsValid()) {
		log_error("Vos::WebView: bridge construction failed");
		return;
	}

	fImpl->fEventAdapter = std::make_unique<EventAdapter>(
		fImpl->fBridge->backend);

	fImpl->fBridge->onTitleChanged = [this](const std::string& t) {
		if (onTitleChanged) onTitleChanged(t);
	};
	fImpl->fBridge->onURLChanged = [this](const std::string& u) {
		if (onURLChanged) onURLChanged(u);
	};
	fImpl->fBridge->onProgressChanged = [this](double p) {
		if (onProgressChanged) onProgressChanged(p);
	};
	fImpl->fBridge->onLoadFinished = [this]() {
		if (onLoadFinished) onLoadFinished();
	};
	fImpl->fBridge->onLoadFailed = [this](const std::string& e) {
		if (onLoadFailed) onLoadFailed(e);
	};
	fImpl->fBridge->onAlert = [this](const std::string& msg) {
		if (onAlert) onAlert(msg);
	};
	fImpl->fBridge->onConfirm = [this](const std::string& msg) -> bool {
		if (onConfirm) return onConfirm(msg);
		return false;
	};
	fImpl->fBridge->onPrompt = [this](const std::string& msg,
		const std::string& def) -> std::string {
		if (onPrompt) return onPrompt(msg, def);
		return def;
	};
	fImpl->fBridge->onNewWindowRequested = [this](const std::string& url) -> bool {
		if (onNewWindowRequested) return onNewWindowRequested(url);
		return false;	// default: refuse popups
	};

	log_info("Vos::WebView created (%dx%d)", fImpl->fWidth, fImpl->fHeight);
}


WebView::~WebView()
{
	log_info("Vos::WebView destroyed");
	// Destruction order: EventAdapter first (stops input dispatch),
	// then Bridge (releases WKView before compositor), then SessionAdapter.
	fImpl->fEventAdapter.reset();
	fImpl->fBridge.reset();
	fImpl->fSessionAdapter.reset();
}


//	#pragma mark - WebView Navigation


#define BRIDGE_GUARD() do { if (!fImpl->fBridge || !fImpl->fBridge->IsValid()) return; } while(0)
#define BRIDGE_GUARD_RET(v) do { if (!fImpl->fBridge || !fImpl->fBridge->IsValid()) return v; } while(0)

void
WebView::LoadURL(const std::string& url)
{
	BRIDGE_GUARD();
	fImpl->fBridge->LoadURL(url);
}


void
WebView::LoadHTML(const std::string& html, const std::string& baseURL)
{
	BRIDGE_GUARD();
	fImpl->fBridge->LoadHTML(html, baseURL);
}


void
WebView::Reload(bool bypassCache)
{
	BRIDGE_GUARD();
	fImpl->fBridge->Reload(bypassCache);
}


void
WebView::Stop()
{
	BRIDGE_GUARD();
	fImpl->fBridge->Stop();
}


bool
WebView::CanGoBack() const
{
	BRIDGE_GUARD_RET(false);
	return fImpl->fBridge->CanGoBack();
}


bool
WebView::CanGoForward() const
{
	BRIDGE_GUARD_RET(false);
	return fImpl->fBridge->CanGoForward();
}


void
WebView::GoBack()
{
	BRIDGE_GUARD();
	fImpl->fBridge->GoBack();
}


void
WebView::GoForward()
{
	BRIDGE_GUARD();
	fImpl->fBridge->GoForward();
}


void
WebView::EvaluateJS(const std::string& script, JSCallback callback)
{
	BRIDGE_GUARD();
	fImpl->fBridge->EvaluateJS(script, callback);
}


void
WebView::AddUserScript(const std::string& js, bool atDocumentStart)
{
	BRIDGE_GUARD();
	fImpl->fBridge->AddUserScript(js, atDocumentStart);
}


void
WebView::AddUserStyleSheet(const std::string& css)
{
	BRIDGE_GUARD();
	fImpl->fBridge->AddUserStyleSheet(css);
}


//	#pragma mark - WebView State


std::string
WebView::CurrentURL() const
{
	BRIDGE_GUARD_RET("");
	return fImpl->fBridge->CurrentURL();
}


std::string
WebView::Title() const
{
	BRIDGE_GUARD_RET("");
	return fImpl->fBridge->Title();
}


double
WebView::LoadProgress() const
{
	BRIDGE_GUARD_RET(0.0);
	return fImpl->fBridge->LoadProgress();
}


bool
WebView::IsLoading() const
{
	BRIDGE_GUARD_RET(false);
	return fImpl->fBridge->IsLoading();
}


//	#pragma mark - WebView Input


void
WebView::HandleKeyEvent(const BMessage* msg)
{
	if (!fImpl->fEventAdapter) return;
	fImpl->fEventAdapter->TranslateKeyDown(msg);
}


void
WebView::HandleMouseEvent(const BMessage* msg)
{
	if (!msg || !fImpl->fEventAdapter) return;
	uint32 what = msg->what;
	if (what == B_MOUSE_DOWN)
		fImpl->fEventAdapter->TranslateMouseDown(msg);
	else if (what == B_MOUSE_UP)
		fImpl->fEventAdapter->TranslateMouseUp(msg);
	else if (what == B_MOUSE_MOVED)
		fImpl->fEventAdapter->TranslateMouseMoved(msg);
	else if (what == B_MOUSE_WHEEL_CHANGED)
		fImpl->fEventAdapter->TranslateMouseWheel(msg);
}


void
WebView::HandleResize(int width, int height)
{
	fImpl->fWidth = width;
	fImpl->fHeight = height;
	BRIDGE_GUARD();
	fImpl->fBridge->Resize(width, height);
}


void
WebView::SetBlockedDomains(const std::vector<std::string>& domains)
{
	BRIDGE_GUARD();
	fImpl->fBridge->SetBlockedDomains(domains);
}


void
WebView::HandleFocusChange(bool focused)
{
	fImpl->fFocused = focused;
	BRIDGE_GUARD();
	fImpl->fBridge->HandleFocusChange(focused);
}


//	#pragma mark - WebView Rendering


BBitmap*
WebView::AcquireFrontBitmap()
{
	BRIDGE_GUARD_RET(nullptr);
	return fImpl->fBridge->compositor->AcquireFront();
}


void
WebView::ReleaseFrontBitmap()
{
	if (!fImpl->fBridge || !fImpl->fBridge->IsValid()) return;
	fImpl->fBridge->compositor->ReleaseFront();
}


int
WebView::NotifyPipeFd() const
{
	BRIDGE_GUARD_RET(-1);
	return fImpl->fBridge->notifier->ReadFd();
}


//	#pragma mark - Session


struct Session::Impl {
	std::string	fProfileDir;
	bool		fEphemeral;
};


Session::Session(const std::string& profileDir, bool ephemeral)
	:
	fImpl(std::make_unique<Impl>())
{
	fImpl->fProfileDir = profileDir;
	fImpl->fEphemeral = ephemeral;
}


Session::~Session()
{
}


void
Session::ClearCookies()
{
}


void
Session::ClearCache()
{
}


std::string
Session::ProfileDir() const
{
	return fImpl->fProfileDir;
}


bool
Session::IsEphemeral() const
{
	return fImpl->fEphemeral;
}


}	// namespace Vos
