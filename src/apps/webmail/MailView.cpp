/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "WebMailStrings.h"
#include "MailView.h"
#include "Providers.h"
#include "UnreadParser.h"
#include "WebMailMessages.h"

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <WebDefs.h>
#include <Window.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace {


// Watches the document title for a "(N)" group (every provider in the
// catalog uses one) and posts the count through the webmail handler.
const char* kBadgeScript =
	"(function(){"
	"function post(n){"
	"try{window.webkit.messageHandlers.webmail.postMessage("
	"JSON.stringify({type:'badge',count:n}));}catch(e){}"
	"}"
	"function scan(){"
	"var m=/\\((\\d+)\\)/.exec(document.title);"
	"post(m?parseInt(m[1],10):0);"
	"}"
	"setInterval(scan,5000);"
	"document.addEventListener('DOMContentLoaded',scan);"
	"scan();"
	"})();";


} // namespace


MailView::MailView(const Account& account)
	:
	BView("mail-view", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fAccount(account),
	fWebView(NULL),
	fSession(NULL),
	fUnread(0),
	fNative(account.kind != WEB_ACCOUNT)
{
	fWebView = new BWebView(Bounds(), "mail-webview");
	fWebView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(fWebView);

	if (fNative) {
		fSession = new MailSession(account, BMessenger(be_app));
		fSession->Start();
	}
}


MailView::~MailView()
{
	delete fSession;
}


void
MailView::AttachedToWindow()
{
	BView::AttachedToWindow();

	BMessenger windowMessenger(Window());
	fWebView->SetTarget(windowMessenger);
	fWebView->AddScriptMessageHandler("webmail");

	if (fNative) {
		// The native UI persists its state (current folder, scroll
		// position) in the account's web profile.
		AccountsService* accounts = DefaultAccountsService();
		fWebView->SetSession(
			accounts->WebProfilePath(fAccount.id.String()).String(),
			false);
		fWebView->SetJavaScriptEnabled(true);
		fWebView->LoadURL("vos-mail:/index.html");
		fSession->RequestFolders();
	} else {
		AccountsService* accounts = DefaultAccountsService();
		fWebView->SetSession(
			accounts->WebProfilePath(fAccount.id.String()).String(),
			false);
		_InjectBadgeScript();

		const WebProvider* provider = FindWebProvider(
			fAccount.provider.String());
		BString url = fAccount.url;
		if (url.Length() == 0 && provider != NULL)
			url = provider->url;
		if (url.Length() == 0)
			url = "about:blank";
		fWebView->LoadURL(url.String());
	}

	ResizeTo(Bounds().Width(), Bounds().Height());
}


void
MailView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	if (fWebView != NULL)
		fWebView->ResizeTo(width, height);
}


void
MailView::_InjectBadgeScript()
{
	// The provider may carry its own badge scraper; the generic title
	// scanner always runs.
	fWebView->AddUserScript(kBadgeScript, true);
	const WebProvider* provider = FindWebProvider(fAccount.provider.String());
	if (provider != NULL && provider->badgeScript != NULL)
		fWebView->AddUserScript(provider->badgeScript, false);
}


void
MailView::Compose(const BString& to, const BString& subject,
	const BString& body)
{
	if (!fNative) {
		const WebProvider* provider = FindWebProvider(
			fAccount.provider.String());
		BString url;
		if (ComposeURLFor(provider, to.String(), url))
			fWebView->LoadURL(url.String());
		return;
	}

	BString script;
	script << "vosMail.openCompose("
		<< "'" << to << "',"
		<< "'" << subject << "',"
		<< "'" << body << "');";
	fWebView->EvaluateJavaScript(script.String(), BMessenger(), 0);
}


void
MailView::CheckMail()
{
	if (fSession != NULL)
		fSession->Sync();
	else if (fWebView != NULL)
		fWebView->Reload();
}


void
MailView::SetUnread(int32 unread)
{
	fUnread = unread;
}


void
MailView::AttachPath(const char* path)
{
	if (fWebView == NULL || path == NULL)
		return;
	BString script;
	script << "vosMail.attach('" << path << "');";
	fWebView->EvaluateJavaScript(script.String(), BMessenger(), 0);
}


void
MailView::HandleScriptMessage(BMessage* message)
{
	const char* body = NULL;
	if (message->FindString(B_WEB_BODY_FIELD, &body) != B_OK || body == NULL)
		return;

	BString json(body);

	// Web accounts post the badge count; the native UI posts commands.
	if (json.IFindFirst("\"badge\"") >= 0) {
		int32 countAt = json.IFindFirst("count");
		if (countAt >= 0) {
			int32 colon = json.FindFirst(':', countAt);
			if (colon >= 0) {
				int32 unread = atoi(json.String() + colon + 1);
				SetUnread(unread);
				BMessage update(MSG_ACCOUNT_UNREAD);
				update.AddString(WEBMAIL_ACCOUNT_ID_FIELD,
					fAccount.id.String());
				update.AddInt32(WEBMAIL_UNREAD_FIELD, unread);
				Window()->PostMessage(&update);
			}
		}
		return;
	}

	if (fSession == NULL)
		return;

	// {cmd:"..."} routing; the compose payload carries its own
	// sub-fields, extracted with simple key scans (the UI and the
	// session share this tiny vocabulary).
	if (json.IFindFirst("\"folders\"") >= 0) {
		fSession->RequestFolders();
	} else if (json.IFindFirst("\"headers\"") >= 0) {
		fSession->RequestHeaders(fCurrentFolder.Length() > 0
			? fCurrentFolder : "INBOX", 1, 50);
	} else if (json.IFindFirst("\"message\"") >= 0) {
		int32 uidAt = json.IFindFirst("uid");
		if (uidAt >= 0) {
			int32 colon = json.FindFirst(':', uidAt);
			if (colon >= 0)
				fSession->RequestMessage(fCurrentFolder,
					(uint32)atoi(json.String() + colon + 1));
		}
	} else if (json.IFindFirst("\"delete\"") >= 0) {
		int32 uidAt = json.IFindFirst("\"uids\"");
		std::vector<uint32> uids;
		while (uidAt >= 0) {
			int32 bracket = json.FindFirst('[', uidAt);
			int32 end = json.FindFirst(']', uidAt);
			if (bracket < 0 || end < 0 || end <= bracket)
				break;
			BString list = StringMid(json, bracket + 1, end - bracket - 1);
			int32 at = 0;
			while (at < list.Length()) {
				int32 comma = list.FindFirst(',', at);
				BString number = comma != B_ERROR
					? StringMid(list, at, comma - at) : StringMid(list, at);
				uids.push_back((uint32)StringInteger(number));
				if (comma == B_ERROR)
					break;
				at = comma + 1;
			}
			break;
		}
		if (uids.size() > 0)
			fSession->DeleteMessages(fCurrentFolder, uids);
	} else if (json.IFindFirst("\"search\"") >= 0) {
		int32 at = json.IFindFirst("criteria");
		if (at >= 0) {
			int32 quote = json.FindFirst('"', at);
			int32 close = quote != B_ERROR
				? json.FindFirst('"', quote + 1) : B_ERROR;
			if (quote >= 0 && close > quote) {
				fSession->Search(fCurrentFolder,
					StringMid(json, quote + 1, close - quote - 1));
			}
		}
	} else if (json.IFindFirst("\"send\"") >= 0
		|| json.IFindFirst("\"draft\"") >= 0) {
		// The full compose object is JSON; the session builds MIME
		// from the MailCompose below. Extract the plain fields.
		MailCompose compose;
		compose.fromAddress = fAccount.address;
		compose.fromName = fAccount.identityName;
		compose.signature = fAccount.signature;
		auto stringField = [&json](const char* key, BString& value) {
			int32 at = json.IFindFirst(key);
			if (at < 0)
				return;
			int32 quote = json.FindFirst('"', at + (int32)strlen(key));
			int32 close = quote != B_ERROR
				? json.FindFirst('"', quote + 1) : B_ERROR;
			if (quote >= 0 && close > quote)
				value = StringMid(json, quote + 1, close - quote - 1);
		};
		stringField("subject", compose.subject);
		stringField("body", compose.bodyText);
		// To/Cc/Bcc: pull every "address":"..." pair after the key.
		auto addressList = [&json](const char* key,
			std::vector<MailAddress>& list) {
			int32 at = json.IFindFirst(key);
			if (at < 0)
				return;
			int32 end = json.FindFirst(']', at);
			if (end < 0)
				return;
			BString section = StringMid(json, at, end - at);
			int32 scan = 0;
			while (true) {
				int32 field = section.IFindFirst("address", scan);
				if (field < 0)
					break;
				int32 quote = section.FindFirst('"', field);
				int32 colon = section.FindFirst(':', quote);
				int32 open = section.FindFirst('"', colon);
				int32 close = section.FindFirst('"', open + 1);
				if (open < 0 || close < 0)
					break;
				MailAddress entry;
				entry.address = StringMid(section, open + 1, close - open - 1);
				list.push_back(entry);
				scan = close;
			}
		};
		addressList("\"to\"", compose.to);
		addressList("\"cc\"", compose.cc);
		addressList("\"bcc\"", compose.bcc);
		if (json.IFindFirst("\"draft\"") >= 0)
			fSession->SaveDraft(compose);
		else
			fSession->SendMessage(compose);
	} else if (json.IFindFirst("\"pickAttachment\"") >= 0) {
		// Attachment picking: the file panel runs on the window
		// thread; the result comes back through MSG_COMPOSE_REQUESTED
		// and lands in the compose form via evaluate.
		BMessage pick(MSG_COMPOSE_REQUESTED);
		pick.AddString(WEBMAIL_ACCOUNT_ID_FIELD, fAccount.id.String());
		Window()->PostMessage(&pick);
	}
}


void
MailView::HandleNativeEvent(BMessage* message)
{
	int32 what = 0;
	message->FindInt32("webmail:what", &what);
	if (what == 0)
		return;

	// Translate session events into page calls. The page exposes
	// vosMail.dispatchEvent(type, payload).
	BString json;
	switch (what) {
		case kNativeEventFolders:
		{
			json = "{\"type\":\"folders\",\"folders\":[";
			int32 count = 0;
			message->FindInt32("webmail:count", &count);
			int32 nameIndex = 0;
			int32 displayIndex = 0;
			for (int32 i = 0; i < count; i++) {
				const char* name = NULL;
				const char* display = NULL;
				if (message->FindString("webmail:name", nameIndex++,
					&name) != B_OK) {
					name = "";
				}
				if (message->FindString("webmail:display", displayIndex++,
					&display) != B_OK) {
					display = name;
				}
				if (i > 0)
					json << ",";
				json << "{\"name\":\"" << name
					<< "\",\"display\":\"" << display << "\"}";
			}
			json << "]}";
			break;
		}
		case kNativeEventHeaders:
		{
			const char* folder = NULL;
			message->FindString("webmail:folder", &folder);
			fCurrentFolder = folder != NULL ? folder : "INBOX";
			json = BString("{\"type\":\"headers\",\"folder\":\"")
				<< fCurrentFolder << "\",\"messages\":[";
			int32 count = 0;
			message->FindInt32("webmail:count", &count);
			int32 uidIndex = 0;
			int32 subjectIndex = 0;
			int32 fromNameIndex = 0;
			int32 fromAddrIndex = 0;
			for (int32 i = 0; i < count; i++) {
				int32 uid = 0;
				message->FindInt32("webmail:uid", uidIndex++, &uid);
				const char* subject = "";
				message->FindString("webmail:subject", subjectIndex++,
					&subject);
				const char* fromName = "";
				message->FindString("webmail:from_name", fromNameIndex++,
					&fromName);
				const char* fromAddr = "";
				message->FindString("webmail:from_addr", fromAddrIndex++,
					&fromAddr);
				if (i > 0)
					json << ",";
				json << "{\"uid\":" << uid
					<< ",\"subject\":\"" << subject
					<< "\",\"fromName\":\"" << fromName
					<< "\",\"fromAddr\":\"" << fromAddr << "\"}";
			}
			json << "]}";
			break;
		}
		case kNativeEventMessage:
		{
			int32 uid = 0;
			message->FindInt32("webmail:uid", &uid);
			const char* subject = "";
			message->FindString("webmail:subject", &subject);
			const char* text = "";
			message->FindString("webmail:text", &text);
			const char* html = "";
			message->FindString("webmail:html", &html);
			json = BString("{\"type\":\"message\",\"uid\":") << uid
				<< ",\"subject\":\"" << subject
				<< "\",\"text\":\"" << text
				<< "\",\"html\":\"" << html << "\"}";
			break;
		}
		case kNativeEventNewMail:
			json = "{\"type\":\"newMail\"}";
			break;
		case kNativeEventSent:
			json = "{\"type\":\"sent\"}";
			break;
		case kNativeEventError:
		{
			const char* error = "";
			message->FindString(WEBMAIL_ERROR_FIELD, &error);
			json = BString("{\"type\":\"error\",\"message\":\"")
				<< error << "\"}";
			break;
		}
		default:
			return;
	}
	_DispatchToPage(json);
}


void
MailView::_DispatchToPage(const BString& json)
{
	if (fWebView == NULL)
		return;
	BString script;
	script << "vosMail.dispatchEvent(" << json << ");";
	fWebView->EvaluateJavaScript(script.String(), BMessenger(), 0);
}
