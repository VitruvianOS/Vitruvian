/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AddShareDialog.h"
#include "NetworkSharesWindow.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <vector>

#include "ShareCore.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkShares"


static const uint32 kMsgOK = 'NSOk';
static const uint32 kMsgCancel = 'NSCa';
static const uint32 kMsgProtocolChanged = 'NSPr';
static const uint32 kMsgModeChanged = 'NSMo';
static const uint32 kMsgLAN = 'NSLa';
static const uint32 kMsgLANResult = 'NSLR';


class LANScanJob {
public:
					LANScanJob(const BMessenger& reply, const char* type)
			:
			fReply(reply),
			fType(type)
	{
	}

	BMessenger	fReply;
	BString		fType;
};


static status_t
_LANScanThread(void* data)
{
	LANScanJob* job = (LANScanJob*)data;
	BMessage found(kMsgLANResult);
	found.AddString("type", job->fType.String());

	FILE* pipe = popen("avahi-browse -t -p 2>/dev/null", "r");
	if (pipe != NULL) {
		char line[1024];
		while (fgets(line, sizeof(line), pipe) != NULL) {
			if (line[0] != '=')
				continue;
			std::vector<BString> fields;
			BString cur;
			for (char* p = line; *p != '\0'; p++) {
				if (*p == ';' || *p == '\n' || *p == '\r') {
					fields.push_back(cur);
					cur = "";
				} else
					cur << *p;
			}
			fields.push_back(cur);
			// avahi-browse -p: =;iface;proto;name;domain;host;ip;port;...
			if (fields.size() < 6)
				continue;
			if (fields[3] != job->fType && job->fType != "") {
				// Some builds print the full type in field 3.
				if (!fields[3].StartsWith(job->fType.String()))
					continue;
			}
			BString host = fields[5];
			BString name = fields[3];
			if (host.IsEmpty())
				continue;
			found.AddString("host", host.String());
			found.AddString("name", name.String());
		}
		pclose(pipe);
	}
	job->fReply.SendMessage(&found);
	delete job;
	return B_OK;
}


static void
_PostLANScan(const BMessenger& reply, const char* type)
{
	LANScanJob* job = new LANScanJob(reply, type);
	thread_id thread = spawn_thread(_LANScanThread, "avahi browse",
		B_NORMAL_PRIORITY, job);
	if (thread < B_OK) {
		delete job;
		return;
	}
	resume_thread(thread);
}


AddShareDialog::AddShareDialog(BWindow* parent, const VosNetworkShareSpec* fill)
	:
	BWindow(BRect(80, 80, 560, 520), B_TRANSLATE("Add network share"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_MODAL_WINDOW
			| B_AVOID_FOCUS),
	fParent(parent),
	fAccepting(false)
{
	if (fill != NULL)
		fSpec = *fill;
	else {
		fSpec.protocol = kShareProtocolNFS;
		fSpec.server = VosNetworkSharesDefaultNFSServer();
		fSpec.exportName = "/export";
		fSpec.mountPoint = std::string(VosNetworkSharesDefaultMountRoot())
			+ "/share";
		fSpec.mode = kShareModeBoot;
	}

	fProtocolMenu = new BPopUpMenu("protocol");
	fProtocolMenu->AddItem(new BMenuItem(B_TRANSLATE("NFS"),
		new BMessage(kMsgProtocolChanged)));
	fProtocolMenu->AddItem(new BMenuItem(B_TRANSLATE("SMB"),
		new BMessage(kMsgProtocolChanged)));
	fProtocolField = new BMenuField(B_TRANSLATE("Protocol:"), fProtocolMenu);

	fLANMenu = new BPopUpMenu("lan");
	fLANMenu->AddItem(new BMenuItem(B_TRANSLATE("Scan for shares"),
		new BMessage(kMsgLAN)));
	fLANField = new BMenuField(B_TRANSLATE("On the network:"), fLANMenu);

	fServerField = new BTextControl("server", B_TRANSLATE("Server:"), "",
		new BMessage(kMsgProtocolChanged));
	fExportField = new BTextControl("export", B_TRANSLATE("Export or share:"), "",
		new BMessage(kMsgProtocolChanged));
	fMountField = new BTextControl("mount", B_TRANSLATE("Mount point:"), "",
		NULL);

	fModeMenu = new BPopUpMenu("mode");
	fModeMenu->AddItem(new BMenuItem(B_TRANSLATE("At boot"),
		new BMessage(kMsgModeChanged)));
	fModeMenu->AddItem(new BMenuItem(B_TRANSLATE("At login"),
		new BMessage(kMsgModeChanged)));
	fModeMenu->AddItem(new BMenuItem(B_TRANSLATE("On first access"),
		new BMessage(kMsgModeChanged)));
	fModeField = new BMenuField(B_TRANSLATE("Mount mode:"), fModeMenu);

	fOptionsField = new BTextControl("options", B_TRANSLATE("Options:"), "",
		NULL);
	fUserField = new BTextControl("user", B_TRANSLATE("User name (SMB):"), "",
		NULL);
	fPasswordField = new BTextControl("password", B_TRANSLATE("Password (SMB):"),
		"", NULL);
	fDomainField = new BTextControl("domain", B_TRANSLATE("Domain (SMB):"), "",
		NULL);

	fOKButton = new BButton("ok", B_TRANSLATE("OK"), new BMessage(kMsgOK));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGrid(B_USE_ITEM_SPACING, B_USE_SMALL_SPACING)
			.AddMenuField(fProtocolField, 0, 0, B_ALIGN_RIGHT)
			.AddMenuField(fLANField, 1, 0, B_ALIGN_RIGHT)
			.AddTextControl(fServerField, 0, 1, B_ALIGN_RIGHT)
			.AddTextControl(fExportField, 0, 2, B_ALIGN_RIGHT)
			.AddTextControl(fMountField, 0, 3, B_ALIGN_RIGHT)
			.AddMenuField(fModeField, 0, 4, B_ALIGN_RIGHT)
			.AddTextControl(fOptionsField, 0, 5, B_ALIGN_RIGHT)
			.AddTextControl(fUserField, 0, 6, B_ALIGN_RIGHT)
			.AddTextControl(fPasswordField, 0, 7, B_ALIGN_RIGHT)
			.AddTextControl(fDomainField, 0, 8, B_ALIGN_RIGHT)
		.End()
		.Add(new BStringView("hints",
			B_TRANSLATE("Boot shares wait at most 15s if the server is "
				"down. Login shares start from the session. First access "
				"uses automount. SMB passwords are stored outside fstab.")))
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fCancelButton)
			.Add(fOKButton)
		.End();

	_FillFromSpec(fSpec);
	_PostLANScan(BMessenger(this), "_nfs._tcp");
	_PostLANScan(BMessenger(this), "_smb._tcp");
}


void
AddShareDialog::Show()
{
	fAccepting = true;
	BWindow::Show();
	Activate();
}


bool
AddShareDialog::QuitRequested()
{
	fAccepting = false;
	return true;
}


void
AddShareDialog::_FillFromSpec(const VosNetworkShareSpec& spec)
{
	fServerField->SetText(spec.server.c_str());
	fExportField->SetText(spec.exportName.c_str());
	fMountField->SetText(spec.mountPoint.c_str());
	fOptionsField->SetText(spec.options.c_str());
	fUserField->SetText(spec.username.c_str());
	fPasswordField->SetText(spec.password.c_str());
	fDomainField->SetText(spec.domain.c_str());

	const char* protocolLabel = spec.protocol == kShareProtocolSMB
		? B_TRANSLATE("SMB") : B_TRANSLATE("NFS");
	BMenuItem* item = fProtocolMenu->FindItem(protocolLabel);
	if (item != NULL)
		item->SetMarked(true);

	const char* modeLabel = spec.mode == kShareModeLogin
		? B_TRANSLATE("At login")
		: spec.mode == kShareModeAutomount
			? B_TRANSLATE("On first access") : B_TRANSLATE("At boot");
	item = fModeMenu->FindItem(modeLabel);
	if (item != NULL)
		item->SetMarked(true);
}


bool
AddShareDialog::_BuildSpecFromFields(VosNetworkShareSpec* spec, BString* error)
{
	VosNetworkShareSpec out = fSpec;
	out.server = fServerField->Text();
	out.exportName = fExportField->Text();
	out.mountPoint = fMountField->Text();
	out.options = fOptionsField->Text();
	out.username = fUserField->Text();
	out.password = fPasswordField->Text();
	out.domain = fDomainField->Text();

	BMenuItem* protocol = fProtocolMenu->FindMarked();
	if (protocol != NULL) {
		bool ok = false;
		out.protocol = VosNetworkSharesParseProtocol(protocol->Label(), &ok);
		if (!ok) {
			if (error != NULL)
				*error = B_TRANSLATE("Choose NFS or SMB.");
			return false;
		}
	}

	BMenuItem* modeItem = fModeMenu->FindMarked();
	if (modeItem != NULL) {
		if (BString(modeItem->Label()) == B_TRANSLATE("At boot"))
			out.mode = kShareModeBoot;
		else if (BString(modeItem->Label()) == B_TRANSLATE("At login"))
			out.mode = kShareModeLogin;
		else
			out.mode = kShareModeAutomount;
	}

	if (out.mountPoint.empty() && !out.exportName.empty()) {
		std::string leaf = out.exportName;
		size_t slash = leaf.find_last_of('/');
		if (slash != std::string::npos && slash + 1 < leaf.size())
			leaf = leaf.substr(slash + 1);
		out.mountPoint = std::string(VosNetworkSharesDefaultMountRoot())
			+ "/" + leaf;
		fMountField->SetText(out.mountPoint.c_str());
	}

	std::string verr;
	if (!VosNetworkSharesValidate(out, &verr)) {
		if (error != NULL)
			*error = verr.c_str();
		return false;
	}
	*spec = out;
	return true;
}


void
AddShareDialog::_AppendLANEntry(BMenu* menu, const char* label,
	const char* server, const char* exportName)
{
	BMessage message(kMsgLAN);
	message.AddString("server", server != NULL ? server : "");
	message.AddString("export", exportName != NULL ? exportName : "");
	menu->AddItem(new BMenuItem(label, new BMessage(message)));
}


void
AddShareDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCancel:
			PostMessage(new BMessage(B_QUIT_REQUESTED));
			return;

		case kMsgOK:
		{
			VosNetworkShareSpec spec;
			BString error;
			if (!_BuildSpecFromFields(&spec, &error)) {
				BAlert* alert = new BAlert(B_TRANSLATE("Network shares"),
					error.String(), B_TRANSLATE("OK"));
				alert->Go();
				return;
			}
			fSpec = spec;
			BMessage result(kMsgHelperDone);
			result.AddString("id", spec.id.c_str());
			result.AddString("protocol",
				VosNetworkSharesProtocolName(spec.protocol));
			result.AddString("server", spec.server.c_str());
			result.AddString("export", spec.exportName.c_str());
			result.AddString("mountpoint", spec.mountPoint.c_str());
			result.AddString("mode", VosNetworkSharesModeName(spec.mode));
			result.AddString("options", spec.options.c_str());
			result.AddString("username", spec.username.c_str());
			result.AddString("password", spec.password.c_str());
			result.AddString("domain", spec.domain.c_str());
			result.AddBool("dialogOk", true);
			fParent->PostMessage(&result);
			PostMessage(new BMessage(B_QUIT_REQUESTED));
			return;
		}

		case kMsgLAN:
			_PostLANScan(BMessenger(this), "_nfs._tcp");
			_PostLANScan(BMessenger(this), "_smb._tcp");
			return;

		case kMsgLANResult:
		{
			int32 index = 0;
			const char* host = NULL;
			const char* name = NULL;
			while (message->FindString("host", index, &host) == B_OK) {
				name = NULL;
				message->FindString("name", index, &name);
				if (host == NULL || host[0] == '\0') {
					index++;
					continue;
				}
				BString label(host);
				if (name != NULL && name[0] != '\0')
					label << " (" << name << ")";
				if (fLANMenu->FindItem(label.String()) == NULL)
					_AppendLANEntry(fLANMenu, label.String(), host, NULL);
				if (fServerField->Text()[0] == '\0')
					fServerField->SetText(host);
				index++;
			}
			return;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
