/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ProxyView.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdlib.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ProxyView"


static const uint32 kModeNone = 'pNon';
static const uint32 kModeManual = 'pMan';
static const uint32 kModeAutomatic = 'pAut';
static const uint32 kMsgApply = 'pApl';
static const uint32 kMsgRevert = 'pRvt';
static const uint32 kMsgFieldModified = 'pFld';
static const uint32 kMsgApplyResult = 'pApR';


class ApplyJob {
public:
							ApplyJob(uint32 mode, const BString& http,
								const BString& https, const BString& ftp,
								const BString& socks, const BString& ignore,
								const BString& pacURL,
								const BMessenger& target)
			:
			fMode(mode),
			fHTTP(http),
			fHTTPS(https),
			fFTP(ftp),
			fSOCKS(socks),
			fIgnore(ignore),
			fPACURL(pacURL),
			fTarget(target)
	{
	}

			uint32			fMode;
			BString			fHTTP;
			BString			fHTTPS;
			BString			fFTP;
			BString			fSOCKS;
			BString			fIgnore;
			BString			fPACURL;
			BMessenger		fTarget;
};


ProxyView::ProxyView()
	:
	BView("proxyView", B_WILL_DRAW),
	fModePopUpMenu(NULL),
	fModeField(NULL),
	fHTTPHostField(NULL),
	fHTTPPortField(NULL),
	fHTTPSHostField(NULL),
	fHTTPSPortField(NULL),
	fFTPHostField(NULL),
	fFTPPortField(NULL),
	fSOCKSHostField(NULL),
	fSOCKSPortField(NULL),
	fIgnoreHostsField(NULL),
	fPACURLField(NULL),
	fApplyButton(NULL),
	fRevertButton(NULL),
	fReasonView(NULL),
	fSnapshotMode(ProxySettings::MODE_NONE),
	fSnapshotHTTPPort(0),
	fSnapshotHTTPSPort(0),
	fSnapshotFTPPort(0),
	fSnapshotSOCKSPort(0),
	fApplyPending(false)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fModePopUpMenu = new BPopUpMenu("proxymodes");
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("None"),
		new BMessage(kModeNone)));
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Manual"),
		new BMessage(kModeManual)));
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Automatic"),
		new BMessage(kModeAutomatic)));

	fModeField = new BMenuField(B_TRANSLATE("Method:"), fModePopUpMenu);
	fModeField->SetToolTip(B_TRANSLATE(
		"None: no proxy. Manual: HTTP/HTTPS/FTP/SOCKS hosts. "
		"Automatic: PAC URL (stored; not honoured by curl/apt yet). "
		"Session apps pick the settings up at the next login."));

	float portWidth = be_control_look->DefaultItemSpacing() * 4;

	fHTTPHostField = new BTextControl(NULL, B_TRANSLATE("HTTP:"), "",
		new BMessage(kMsgFieldModified));
	fHTTPPortField = new BTextControl(NULL, "", "",
		new BMessage(kMsgFieldModified));
	fHTTPPortField->TextView()->SetExplicitMaxSize(
		BSize(portWidth, B_SIZE_UNSET));
	fHTTPPortField->SetToolTip(B_TRANSLATE("HTTP proxy port"));

	fHTTPSHostField = new BTextControl(NULL, B_TRANSLATE("HTTPS:"), "",
		new BMessage(kMsgFieldModified));
	fHTTPSPortField = new BTextControl(NULL, "", "",
		new BMessage(kMsgFieldModified));
	fHTTPSPortField->TextView()->SetExplicitMaxSize(
		BSize(portWidth, B_SIZE_UNSET));
	fHTTPSPortField->SetToolTip(B_TRANSLATE("HTTPS proxy port"));

	fFTPHostField = new BTextControl(NULL, B_TRANSLATE("FTP:"), "",
		new BMessage(kMsgFieldModified));
	fFTPPortField = new BTextControl(NULL, "", "",
		new BMessage(kMsgFieldModified));
	fFTPPortField->TextView()->SetExplicitMaxSize(
		BSize(portWidth, B_SIZE_UNSET));
	fFTPPortField->SetToolTip(B_TRANSLATE("FTP proxy port"));

	fSOCKSHostField = new BTextControl(NULL, B_TRANSLATE("SOCKS:"), "",
		new BMessage(kMsgFieldModified));
	fSOCKSPortField = new BTextControl(NULL, "", "",
		new BMessage(kMsgFieldModified));
	fSOCKSPortField->TextView()->SetExplicitMaxSize(
		BSize(portWidth, B_SIZE_UNSET));
	fSOCKSPortField->SetToolTip(B_TRANSLATE("SOCKS proxy port"));

	float minimumWidth = be_control_look->DefaultItemSpacing() * 15;

	fIgnoreHostsField = new BTextControl(NULL, B_TRANSLATE("Ignore hosts:"),
		"", new BMessage(kMsgFieldModified));
	fIgnoreHostsField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fIgnoreHostsField->SetToolTip(B_TRANSLATE(
		"Comma-separated hosts that bypass the proxy "
		"(default: localhost, 127.0.0.0/8, ::1)"));

	fPACURLField = new BTextControl(NULL, B_TRANSLATE("PAC URL:"), "",
		new BMessage(kMsgFieldModified));
	fPACURLField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fPACURLField->SetToolTip(B_TRANSLATE(
		"URL for Proxy Auto-Configuration (Automatic mode only)"));

	fApplyButton = new BButton("applyProxy", B_TRANSLATE("Apply"),
		new BMessage(kMsgApply));
	fRevertButton = new BButton("revertProxy", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));

	fReasonView = new BStringView(NULL, "");
	fReasonView->SetFont(be_plain_font);
	fReasonView->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_3_TINT));
	fReasonView->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_UNSET));

	Reload();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.AddGrid(B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING)
			.AddMenuField(fModeField, 0, 0, B_ALIGN_RIGHT)
			.AddTextControl(fHTTPHostField, 0, 1, B_ALIGN_RIGHT)
			.Add(fHTTPPortField, 1, 1)
			.AddTextControl(fHTTPSHostField, 0, 2, B_ALIGN_RIGHT)
			.Add(fHTTPSPortField, 1, 2)
			.AddTextControl(fFTPHostField, 0, 3, B_ALIGN_RIGHT)
			.Add(fFTPPortField, 1, 3)
			.AddTextControl(fSOCKSHostField, 0, 4, B_ALIGN_RIGHT)
			.Add(fSOCKSPortField, 1, 4)
			.AddTextControl(fIgnoreHostsField, 0, 5, B_ALIGN_RIGHT)
			.AddTextControl(fPACURLField, 0, 6, B_ALIGN_RIGHT)
		.End()
		.Add(fReasonView)
		.AddGroup(B_HORIZONTAL)
			.Add(fRevertButton)
			.AddGlue()
			.Add(fApplyButton)
		.End();

	_UpdateApplyState();
}


ProxyView::~ProxyView()
{
}


void
ProxyView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fModePopUpMenu->SetTargetForItems(this);
	fApplyButton->SetTarget(this);
	fRevertButton->SetTarget(this);
	fHTTPHostField->SetTarget(this);
	fHTTPPortField->SetTarget(this);
	fHTTPSHostField->SetTarget(this);
	fHTTPSPortField->SetTarget(this);
	fFTPHostField->SetTarget(this);
	fFTPPortField->SetTarget(this);
	fSOCKSHostField->SetTarget(this);
	fSOCKSPortField->SetTarget(this);
	fIgnoreHostsField->SetTarget(this);
	fPACURLField->SetTarget(this);
}


void
ProxyView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kModeNone:
		case kModeManual:
		case kModeAutomatic:
			_SetMode(message->what);
			_UpdateApplyState();
			break;

		case kMsgFieldModified:
			_UpdateApplyState();
			break;

		case kMsgApply:
			_DoApply();
			break;

		case kMsgRevert:
			Revert();
			break;

		case kMsgApplyResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			BString reason;
			message->FindString("reason", &reason);
			_ApplyResult(status == B_OK, reason);
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


void
ProxyView::Reload()
{
	fSettings.Load();
	_FillFieldsFromSettings();
	_CaptureSnapshot();
	_UpdateApplyState();
}


bool
ProxyView::IsDirty() const
{
	if (fHTTPHostField == NULL)
		return false;

	return _Mode() != fSnapshotMode
		|| BString(fHTTPHostField->Text()) != fSnapshotHTTPHost
		|| (uint16)atoi(fHTTPPortField->Text()) != fSnapshotHTTPPort
		|| BString(fHTTPSHostField->Text()) != fSnapshotHTTPSHost
		|| (uint16)atoi(fHTTPSPortField->Text()) != fSnapshotHTTPSPort
		|| BString(fFTPHostField->Text()) != fSnapshotFTPHost
		|| (uint16)atoi(fFTPPortField->Text()) != fSnapshotFTPPort
		|| BString(fSOCKSHostField->Text()) != fSnapshotSOCKSHost
		|| (uint16)atoi(fSOCKSPortField->Text()) != fSnapshotSOCKSPort
		|| BString(fIgnoreHostsField->Text()) != fSnapshotIgnoreHosts
		|| BString(fPACURLField->Text()) != fSnapshotPACURL;
}


void
ProxyView::Revert()
{
	_SetMode(fSnapshotMode);
	fHTTPHostField->SetText(fSnapshotHTTPHost.String());
	fHTTPPortField->SetText(BString() << fSnapshotHTTPPort);
	fHTTPSHostField->SetText(fSnapshotHTTPSHost.String());
	fHTTPSPortField->SetText(BString() << fSnapshotHTTPSPort);
	fFTPHostField->SetText(fSnapshotFTPHost.String());
	fFTPPortField->SetText(BString() << fSnapshotFTPPort);
	fSOCKSHostField->SetText(fSnapshotSOCKSHost.String());
	fSOCKSPortField->SetText(BString() << fSnapshotSOCKSPort);
	fIgnoreHostsField->SetText(fSnapshotIgnoreHosts.String());
	fPACURLField->SetText(fSnapshotPACURL.String());
	_UpdateApplyState();
}


const char*
ProxyView::ReasonApplyDisabled() const
{
	if (fApplyPending)
		return B_TRANSLATE("Applying" B_UTF8_ELLIPSIS);

	if (!IsDirty())
		return B_TRANSLATE("No unapplied changes");

	uint32 mode = _Mode();
	if (mode == kModeManual) {
		// An empty Manual would read as None everywhere.
		if (BString(fHTTPHostField->Text()).IsEmpty()
			&& BString(fHTTPSHostField->Text()).IsEmpty()
			&& BString(fFTPHostField->Text()).IsEmpty()
			&& BString(fSOCKSHostField->Text()).IsEmpty()) {
			return B_TRANSLATE("Manual mode needs at least one proxy host");
		}
	} else if (mode == kModeAutomatic) {
		if (BString(fPACURLField->Text()).IsEmpty())
			return B_TRANSLATE("Automatic mode needs a PAC URL");
	}

	return NULL;
}


void
ProxyView::_CaptureSnapshot()
{
	fSnapshotMode = _Mode();
	fSnapshotHTTPHost = fHTTPHostField->Text();
	fSnapshotHTTPPort = (uint16)atoi(fHTTPPortField->Text());
	fSnapshotHTTPSHost = fHTTPSHostField->Text();
	fSnapshotHTTPSPort = (uint16)atoi(fHTTPSPortField->Text());
	fSnapshotFTPHost = fFTPHostField->Text();
	fSnapshotFTPPort = (uint16)atoi(fFTPPortField->Text());
	fSnapshotSOCKSHost = fSOCKSHostField->Text();
	fSnapshotSOCKSPort = (uint16)atoi(fSOCKSPortField->Text());
	fSnapshotIgnoreHosts = fIgnoreHostsField->Text();
	fSnapshotPACURL = fPACURLField->Text();
}


void
ProxyView::_UpdateApplyState()
{
	const char* reason = ReasonApplyDisabled();
	fReasonView->SetText(reason != NULL ? reason : "");
	fApplyButton->SetEnabled(reason == NULL);
	fRevertButton->SetEnabled(IsDirty());

	if (Window() != NULL)
		Window()->PostMessage(kMsgDirtyChanged);
}


void
ProxyView::_SetMode(uint32 mode)
{
	BMenuItem* item = fModePopUpMenu->FindItem(mode);
	if (item != NULL)
		item->SetMarked(true);

	_EnableFields(mode == kModeManual);
	fPACURLField->SetEnabled(mode == kModeAutomatic);
}


void
ProxyView::_EnableFields(bool enable)
{
	fHTTPHostField->SetEnabled(enable);
	fHTTPPortField->SetEnabled(enable);
	fHTTPSHostField->SetEnabled(enable);
	fHTTPSPortField->SetEnabled(enable);
	fFTPHostField->SetEnabled(enable);
	fFTPPortField->SetEnabled(enable);
	fSOCKSHostField->SetEnabled(enable);
	fSOCKSPortField->SetEnabled(enable);
	fIgnoreHostsField->SetEnabled(enable);
}


uint32
ProxyView::_Mode() const
{
	uint32 mode = kModeNone;
	BMenuItem* item = fModePopUpMenu->FindMarked();
	if (item != NULL)
		mode = item->Message()->what;

	return mode;
}


void
ProxyView::_DoApply()
{
	if (ReasonApplyDisabled() != NULL)
		return;

	_ReadFieldsIntoSettings();
	fSettings.Save();

	// pkexec may sit on an auth dialog; never on the window thread.
	fApplyPending = true;
	_UpdateApplyState();

	uint32 mode = _Mode() == kModeManual ? ProxySettings::MODE_MANUAL
		: (_Mode() == kModeAutomatic ? ProxySettings::MODE_AUTOMATIC
			: ProxySettings::MODE_NONE);

	BString http;
	BString https;
	BString ftp;
	BString socks;
	BString ignore = fIgnoreHostsField->Text();
	if (ignore.IsEmpty())
		ignore = ProxySettings::DefaultIgnoreHosts();

	if (mode == ProxySettings::MODE_MANUAL) {
		// Format host:port here so the job does not need a live
		// ProxySettings (SettingsMessage is not copyable).
		_FormatPair(fHTTPHostField, fHTTPPortField, http);
		_FormatPair(fHTTPSHostField, fHTTPSPortField, https);
		_FormatPair(fFTPHostField, fFTPPortField, ftp);
		_FormatPair(fSOCKSHostField, fSOCKSPortField, socks);
	}

	ApplyJob* job = new ApplyJob(mode, http, https, ftp, socks, ignore,
		fPACURLField->Text(), BMessenger(this));
	thread_id thread = spawn_thread(&ProxyView::_ApplyThread, "proxy apply",
		B_NORMAL_PRIORITY, job);
	if (thread < 0) {
		delete job;
		fApplyPending = false;
		_UpdateApplyState();
		return;
	}
	resume_thread(thread);
}


void
ProxyView::_FormatPair(BTextControl* hostField, BTextControl* portField,
	BString& out) const
{
	const char* host = hostField->Text();
	int port = atoi(portField->Text());
	if (host == NULL || host[0] == '\0' || port <= 0) {
		out = "";
		return;
	}
	out = host;
	out << ":" << port;
}


/*static*/ status_t
ProxyView::_ApplyThread(void* data)
{
	ApplyJob* job = reinterpret_cast<ApplyJob*>(data);

	BString reason;
	bool ok = ProxySettings::ApplyWith(job->fMode, job->fHTTP, job->fHTTPS,
		job->fFTP, job->fSOCKS, job->fIgnore, job->fPACURL, &reason);

	BMessage* result = new BMessage(kMsgApplyResult);
	result->AddInt32("status", ok ? B_OK : B_ERROR);
	if (!ok)
		result->AddString("reason", reason.String());
	job->fTarget.SendMessage(result);
	delete job;
	return B_OK;
}


void
ProxyView::_ApplyResult(bool ok, const BString& reason)
{
	fApplyPending = false;
	if (ok) {
		_CaptureSnapshot();
	} else {
		BString text(B_TRANSLATE("Could not apply the proxy settings."));
		if (!reason.IsEmpty())
			text << "\n" << reason;
		BAlert* alert = new BAlert(B_TRANSLATE("Not applied"),
			text.String(), B_TRANSLATE("OK"));
		alert->Go(NULL);
	}
	_UpdateApplyState();
}


void
ProxyView::_FillFieldsFromSettings()
{
	_SetMode(fSettings.Mode() == ProxySettings::MODE_MANUAL
		? kModeManual
		: (fSettings.Mode() == ProxySettings::MODE_AUTOMATIC
			? kModeAutomatic : kModeNone));

	_SetField(fHTTPHostField, fSettings.HTTPHost().String());
	_SetPort(fHTTPPortField, fSettings.HTTPPort());
	_SetField(fHTTPSHostField, fSettings.HTTPSHost().String());
	_SetPort(fHTTPSPortField, fSettings.HTTPSPort());
	_SetField(fFTPHostField, fSettings.FTPHost().String());
	_SetPort(fFTPPortField, fSettings.FTPPort());
	_SetField(fSOCKSHostField, fSettings.SOCKSHost().String());
	_SetPort(fSOCKSPortField, fSettings.SOCKSPort());
	_SetField(fIgnoreHostsField, fSettings.IgnoreHosts().String());
	_SetField(fPACURLField, fSettings.PACURL().String());
}


void
ProxyView::_ReadFieldsIntoSettings()
{
	uint32 mode = _Mode();
	fSettings.SetMode(mode == kModeManual ? ProxySettings::MODE_MANUAL
		: (mode == kModeAutomatic ? ProxySettings::MODE_AUTOMATIC
			: ProxySettings::MODE_NONE));

	fSettings.SetHTTPHost(fHTTPHostField->Text());
	fSettings.SetHTTPPort((uint16)atoi(fHTTPPortField->Text()));
	fSettings.SetHTTPSHost(fHTTPSHostField->Text());
	fSettings.SetHTTPSPort((uint16)atoi(fHTTPSPortField->Text()));
	fSettings.SetFTPHost(fFTPHostField->Text());
	fSettings.SetFTPPort((uint16)atoi(fFTPPortField->Text()));
	fSettings.SetSOCKSHost(fSOCKSHostField->Text());
	fSettings.SetSOCKSPort((uint16)atoi(fSOCKSPortField->Text()));
	fSettings.SetIgnoreHosts(fIgnoreHostsField->Text());
	fSettings.SetPACURL(fPACURLField->Text());

	if (fSettings.IgnoreHosts().IsEmpty())
		fSettings.SetIgnoreHosts(ProxySettings::DefaultIgnoreHosts());
}


void
ProxyView::_SetField(BTextControl* control, const char* text)
{
	if (control != NULL)
		control->SetText(text != NULL ? text : "");
}


void
ProxyView::_SetPort(BTextControl* control, uint16 port)
{
	if (control == NULL)
		return;
	if (port == 0)
		control->SetText("");
	else {
		BString text;
		text << port;
		control->SetText(text.String());
	}
}
