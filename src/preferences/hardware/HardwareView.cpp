/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HardwareView.h"

#include <AppDefs.h>
#include <Button.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <String.h>
#include <StringView.h>
#include <TabView.h>

#include <time.h>

#include "AccelerationView.h"
#include "DeviceTreeView.h"
#include "HardwareHelperClient.h"
#include "HardwareMessages.h"
#include "HardwareReport.h"
#include "MachineInfoReader.h"
#include "UdevScanner.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


static const uint32 kMsgCopy = 'HWcp';
static const uint32 kMsgExport = 'HWex';
static const uint32 kMsgRefresh = 'HWrf';
static const uint32 kMsgDetails = 'HWdt';


HardwareView::HardwareView()
	:
	BView("hardware", B_WILL_DRAW | B_FRAME_EVENTS),
	fScanner(NULL),
	fFilePanel(NULL),
	fHelperRunning(false)
{
	fNames.LoadPci(Hardware::HwdataDatabase::FindPciIds().c_str());
	fNames.LoadUsb(Hardware::HwdataDatabase::FindUsbIds().c_str());
	fMachine = Hardware::ReadMachineInfo("/sys", "/proc");

	fSummaryView = new BStringView("summary", "", B_WILL_DRAW);
	BFont summaryFont = be_plain_font;
	summaryFont.SetSize(12);
	fSummaryView->SetFont(&summaryFont);

	fPlayView = new DeviceTreeView(Hardware::kTabPlugPlay, this);
	fPlatformView = new DeviceTreeView(Hardware::kTabPlatform, this);

	fTabView = new BTabView("tabs", B_WILL_DRAW | B_FRAME_EVENTS);
	BTab* playTab = new BTab();
	fTabView->AddTab(fPlayView, playTab);
	playTab->SetLabel(B_TRANSLATE("Plug and Play"));
	BTab* platformTab = new BTab();
	fTabView->AddTab(fPlatformView, platformTab);
	platformTab->SetLabel(B_TRANSLATE("Platform"));
	BTab* accelerationTab = new BTab();
	fTabView->AddTab(new AccelerationView(), accelerationTab);
	accelerationTab->SetLabel(B_TRANSLATE("Acceleration"));

	fCopyButton = new BButton("copy", B_TRANSLATE("Copy info"),
		new BMessage(kMsgCopy));
	fExportButton = new BButton("export",
		B_TRANSLATE("Export full report..."), new BMessage(kMsgExport));
	fRefreshButton = new BButton("refresh", B_TRANSLATE("Refresh"),
		new BMessage(kMsgRefresh));
	fDetailsButton = new BButton("details",
		B_TRANSLATE("Show full details..."), new BMessage(kMsgDetails));

	fCopyButton->SetEnabled(false);
	fDetailsButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(fSummaryView)
		.Add(fTabView, 1.0)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fCopyButton)
			.Add(fExportButton)
			.Add(fRefreshButton)
			.Add(fDetailsButton)
			.AddGlue()
		.End()
		.SetInsets(B_USE_WINDOW_SPACING);

	_UpdateSummary();
}


HardwareView::~HardwareView()
{
	delete fScanner;
	delete fFilePanel;
}


void
HardwareView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (fScanner == NULL) {
		fScanner = new UdevScanner();
		fScanner->Start(BMessenger(this));
	}
}


void
HardwareView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDevicesUpdated:
			_ApplyRecords();
			break;

		case kMsgListSelection:
			_UpdateActions();
			break;

		case kMsgCopy:
			_CopyInfo();
			break;

		case kMsgExport:
			_ShowExportPanel();
			break;

		case kMsgRefresh:
			if (fScanner != NULL)
				fScanner->RequestRescan();
			break;

		case kMsgDetails:
			_StartHelper();
			break;

		case kMsgHelperResult:
		{
			fHelperRunning = false;
			bool ok = false;
			message->FindBool("ok", &ok);
			if (ok) {
				const char* text = NULL;
				if (message->FindString("text", &text) == B_OK)
					fPrivilegedText = text;
				fPlayView->SetPrivilegedText(fPrivilegedText);
				fPlatformView->SetPrivilegedText(fPrivilegedText);
			}
			// Denied or cancelled: keep the unprivileged view.
			_UpdateActions();
			break;
		}

		case B_SAVE_PANEL:
		{
			entry_ref directory;
			BString name;
			if (message->FindRef("directory", &directory) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BPath dir(&directory);
				_ExportReport(BPath(dir.Path(), name.String()));
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
HardwareView::_UpdateSummary()
{
	BString text;
	text << B_TRANSLATE("Model: ") << fMachine.model.c_str() << "\n";
	text << B_TRANSLATE("CPU: ") << fMachine.cpuModel.c_str();
	if (!fMachine.cpuCores.empty())
		text << " (" << fMachine.cpuCores.c_str()
			<< B_TRANSLATE(" cores)");
	if (!fMachine.cpuFrequency.empty())
		text << " @ " << fMachine.cpuFrequency.c_str();
	text << "\n";
	text << B_TRANSLATE("Memory: ") << fMachine.memory.c_str()
		<< "    " << B_TRANSLATE("Firmware: ")
		<< fMachine.firmware.c_str() << "\n";
	text << B_TRANSLATE("Kernel: ") << fMachine.kernel.c_str()
		<< " (" << fMachine.architecture.c_str() << ")";

	fSummaryView->SetText(text.String());
}


void
HardwareView::_ApplyRecords()
{
	if (fScanner == NULL)
		return;

	std::vector<Hardware::DeviceRecord> records = fScanner->Records();
	fPlayView->SetRecords(records, &fNames);
	fPlatformView->SetRecords(records, &fNames);
	_UpdateActions();
}


void
HardwareView::_UpdateActions()
{
	bool hasSelection = fPlayView->SelectedRecord() != NULL
		|| fPlatformView->SelectedRecord() != NULL;

	fCopyButton->SetEnabled(hasSelection);
	fDetailsButton->SetEnabled(!fHelperRunning);
}


void
HardwareView::_CopyInfo()
{
	DeviceTreeView* tab = _CurrentTab();
	if (tab == NULL)
		return;

	std::string text = tab->SelectedDetail(true);
	if (text.empty())
		return;

	BClipboard clipboard("system");
	if (!clipboard.Lock())
		return;

	clipboard.Clear();
	BMessage* data = new BMessage(B_SIMPLE_DATA);
	data->AddString("text/plain", text.c_str());
	clipboard.Commit(data);
	clipboard.Unlock();
}


void
HardwareView::_ShowExportPanel()
{
	if (fFilePanel == NULL) {
		fFilePanel = new BFilePanel(B_SAVE_PANEL,
			new BMessenger(this));
		fFilePanel->SetSaveText("hardware-report.txt");
	}

	fFilePanel->Show();
}


void
HardwareView::_ExportReport(const BPath& path)
{
	if (fScanner == NULL)
		return;

	std::vector<Hardware::DeviceRecord> records = fScanner->Records();

	char stamp[32];
	time_t now = time(NULL);
	struct tm utc;
	gmtime_r(&now, &utc);
	strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);

	std::string report = Hardware::BuildReport(fMachine, records,
		fNames, stamp);

	if (!fPrivilegedText.empty()) {
		report += "\nPrivileged details (administrator assisted)\n";
		report += fPrivilegedText;
		if (report[report.size() - 1] != '\n')
			report += "\n";
	}

	FILE* file = fopen(path.Path(), "w");
	if (file == NULL)
		return;

	fwrite(report.data(), 1, report.size(), file);
	fclose(file);
}


void
HardwareView::_StartHelper()
{
	if (fHelperRunning)
		return;

	fHelperRunning = true;
	_UpdateActions();

	if (HardwareHelperRunAsync(BMessenger(this)) != B_OK) {
		fHelperRunning = false;
		_UpdateActions();
	}
}


DeviceTreeView*
HardwareView::_CurrentTab() const
{
	if (fTabView == NULL)
		return NULL;

	BView* view = fTabView->ChildAt(fTabView->Selection());
	if (view == fPlayView)
		return fPlayView;
	if (view == fPlatformView)
		return fPlatformView;

	return NULL;
}
