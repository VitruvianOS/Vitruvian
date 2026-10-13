/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "NetPlusApp.h"
#include "NetPlusMessages.h"
#include "SettingsWindow.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <String.h>
#include <TextControl.h>

#include <web/WebView.h>
#include <web/WebSettings.h>

#include <cstdio>


static const uint32 kMsgOK = 'stok';
static const uint32 kMsgCancel = 'stca';
static const uint32 kMsgBrowse = 'stbr';
static const uint32 kMsgBrowseDone = 'stbd';
static const uint32 kMsgClearData = 'stcl';
static const uint32 kMsgEngineChanged = 'stec';

static SettingsWindow* sInstance = NULL;


/* static */
void
SettingsWindow::ShowWindow(BRect frame)
{
	if (sInstance == NULL) {
		sInstance = new SettingsWindow(frame);
		sInstance->Show();
	} else if (sInstance->IsHidden()) {
		sInstance->Show();
	} else {
		sInstance->Activate();
	}
}


SettingsWindow::SettingsWindow(BRect frame)
	:
	BWindow(frame, "NetPlus Settings", B_TITLED_WINDOW,
		B_NOT_CLOSABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE),
	fHomepage(NULL),
	fSearchEngine(NULL),
	fCustomTemplate(NULL),
	fDownloadDir(NULL),
	fBrowseButton(NULL),
	fAskDownload(NULL),
	fJavaScript(NULL),
	fImages(NULL),
	fUserAgentSuffix(NULL),
	fBlockedDomains(NULL),
	fPrivateBrowsing(NULL),
	fZoomDefault(NULL),
	fRestoreSession(NULL),
	fClearDataButton(NULL)
{
	fHomepage = new BTextControl("Homepage", "", NULL);
	fCustomTemplate = new BTextControl("Custom search URL (%s = query)", "", NULL);

	BPopUpMenu* engines = new BPopUpMenu("engines");
	const char* kEngines[] = {
		"DuckDuckGo", "Startpage", "Brave Search", "Google", "Bing",
		"Wikipedia", "Custom"
	};
	for (const char* name : kEngines)
		engines->AddItem(new BMenuItem(name, NULL));
	fSearchEngine = new BMenuField("Search engine", engines,
		new BMessage(kMsgEngineChanged));

	fDownloadDir = new BTextControl("Download folder", "", NULL);
	fBrowseButton = new BButton("Browse", new BMessage(kMsgBrowse));
	fAskDownload = new BCheckBox("Ask where to save each download", NULL);
	fJavaScript = new BCheckBox("Enable JavaScript", NULL);
	fImages = new BCheckBox("Load images", NULL);
	fUserAgentSuffix = new BTextControl("User-agent suffix", "", NULL);
	fBlockedDomains = new BTextControl("Blocked domains (comma list)", "", NULL);
	fPrivateBrowsing = new BCheckBox(
		"Private browsing (no cookies or history kept)", NULL);
	fZoomDefault = new BTextControl("Default zoom (1.00 = 100%)", "", NULL);
	fRestoreSession = new BCheckBox("Restore the last page on start", NULL);
	fClearDataButton = new BButton("Clear browsing data",
		new BMessage(kMsgClearData));

	BButton* okButton = new BButton("OK", new BMessage(kMsgOK));
	BButton* cancelButton = new BButton("Cancel", new BMessage(kMsgCancel));
	okButton->MakeDefault(true);

	_LoadFromSettings();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fHomepage)
		.Add(fSearchEngine)
		.Add(fCustomTemplate)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fDownloadDir, 1.0f)
			.Add(fBrowseButton)
		.End()
		.Add(fAskDownload)
		.Add(fJavaScript)
		.Add(fImages)
		.Add(fUserAgentSuffix)
		.Add(fBlockedDomains)
		.Add(fPrivateBrowsing)
		.Add(fZoomDefault)
		.Add(fRestoreSession)
		.Add(fClearDataButton)
		.AddStrut(B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(okButton)
			.Add(cancelButton)
		.End();

	_UpdateCustomTemplateEnabled();
}


SettingsWindow::~SettingsWindow()
{
	sInstance = NULL;
}


void
SettingsWindow::_LoadFromSettings()
{
	BWebSettings* settings = NetPlusApp::Settings();

	fHomepage->SetText(settings->Homepage());

	const char* engine = settings->SearchEngine();
	BMenu* menu = fSearchEngine->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item != NULL && strcmp(item->Label(), engine) == 0)
			item->SetMarked(true);
	}
	if (menu->FindMarked() == NULL)
		menu->ItemAt(menu->CountItems() - 1)->SetMarked(true);

	fCustomTemplate->SetText(settings->SearchURLTemplate());

	const char* dir = settings->DownloadDir();
	if (dir != NULL)
		fDownloadDir->SetText(dir);
	fAskDownload->SetValue(settings->AskDownloadLocation());
	fJavaScript->SetValue(settings->JavaScriptEnabled());
	fImages->SetValue(settings->ImagesEnabled());

	const char* suffix = settings->UserAgentSuffix();
	if (suffix != NULL)
		fUserAgentSuffix->SetText(suffix);

	const char* blocked = settings->BlockedDomains();
	if (blocked != NULL)
		fBlockedDomains->SetText(blocked);

	fPrivateBrowsing->SetValue(settings->PrivateBrowsing());

	char zoom[32];
	snprintf(zoom, sizeof(zoom), "%.2f", settings->ZoomDefault());
	fZoomDefault->SetText(zoom);

	fRestoreSession->SetValue(settings->RestoreSession());
}


void
SettingsWindow::_UpdateCustomTemplateEnabled()
{
	BMenuItem* marked = fSearchEngine->Menu()->FindMarked();
	bool custom = marked != NULL
		&& strcmp(marked->Label(), "Custom") == 0;
	fCustomTemplate->SetEnabled(custom);
}


void
SettingsWindow::_ApplyAndSave()
{
	BWebSettings* settings = NetPlusApp::Settings();

	settings->SetHomepage(fHomepage->Text());

	BMenuItem* marked = fSearchEngine->Menu()->FindMarked();
	const char* engine = marked != NULL ? marked->Label() : "DuckDuckGo";
	settings->SetSearchEngine(engine);
	if (NetPlusApp::SearchTemplateFor(engine) == NULL)
		settings->SetSearchURLTemplate(fCustomTemplate->Text());

	settings->SetDownloadDir(fDownloadDir->Text());
	settings->SetAskDownloadLocation(fAskDownload->Value());
	settings->SetJavaScriptEnabled(fJavaScript->Value());
	settings->SetImagesEnabled(fImages->Value());
	settings->SetUserAgentSuffix(fUserAgentSuffix->Text());
	settings->SetBlockedDomains(fBlockedDomains->Text());
	settings->SetPrivateBrowsing(fPrivateBrowsing->Value());
	settings->SetRestoreSession(fRestoreSession->Value());

	double zoom = strtod(fZoomDefault->Text(), NULL);
	if (zoom < 0.25)
		zoom = 0.25;
	if (zoom > 5.0)
		zoom = 5.0;
	settings->SetZoomDefault(zoom);

	settings->Save();
	static_cast<NetPlusApp*>(be_app)->BroadcastSettingsChanged();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEngineChanged:
			_UpdateCustomTemplateEnabled();
			break;

		case kMsgOK:
			_ApplyAndSave();
			Close();
			break;

		case kMsgCancel:
			Close();
			break;

		case kMsgBrowse:
		{
			BMessage panelMessage(kMsgBrowseDone);
			BFilePanel* panel = new BFilePanel(B_OPEN_PANEL,
				new BMessenger(this), NULL, 0, false, &panelMessage);
			panel->Show();
			break;
		}

		case kMsgBrowseDone:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					fDownloadDir->SetText(path.Path());
			}
			break;
		}

		case kMsgClearData:
		{
			BAlert* confirm = new BAlert("Clear browsing data",
				"Delete cookies, cache and local storage for the\n"
				"shared profile? Open pages keep working; sites\n"
				"will ask to log in again.", "Cancel", "Clear");
			if (confirm->Go() == 1) {
				BWebView::ClearDefaultProfileData();
				BAlert* done = new BAlert("Clear browsing data",
					"Browsing data cleared.", "OK");
				done->Go(NULL);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
SettingsWindow::QuitRequested()
{
	return true;
}
