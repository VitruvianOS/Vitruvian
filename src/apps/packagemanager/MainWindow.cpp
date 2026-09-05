/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MainWindow.h"

#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Entry.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Size.h>
#include <SplitView.h>
#include <TabView.h>

#include "AptLogView.h"
#include "FilterView.h"
#include "PackageInfo.h"
#include "PackageInfoView.h"
#include "PackageListView.h"
#include "PackageWorker.h"
#include "WorkStatusView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow(BRect frame)
	:
	Inherited(frame, B_TRANSLATE_SYSTEM_NAME(
		"PackageManager"), B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fPackages(50),
	fTransactionActive(false)
{
	_BuildMenuBar();
	_BuildLayout();

	fWorker = new PackageWorker(BMessenger(this));
	fWorker->Run();

	fWorker->PostMessage(kMsgRefreshList);
	fWorker->PostMessage(kMsgLoadLog);

	_WatchDpkgStatus();
}


MainWindow::~MainWindow()
{
	stop_watching(BMessenger(this));

	if (fWorker != NULL && fWorker->Lock())
		fWorker->Quit();
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFilterChanged:
			_ApplyFilter();
			break;

		case kMsgSectionSelected:
		{
			const char* section = "";
			message->FindString("section", &section);
			fFilterView->SetSection(section);
			_ApplyFilter();
			break;
		}

		case kMsgStatusSelected:
		{
			int32 status = kFilterAll;
			message->FindInt32("status", &status);
			fFilterView->SetStatus((status_filter)status);
			_ApplyFilter();
			break;
		}

		case kMsgPackageSelected:
		{
			PackageInfo* package = fListView->SelectedPackage();
			fInfoView->SetPackage(package);
			if (package == NULL || package->HasDetails())
				break;

			BMessage request(kMsgLoadDetails);
			request.AddString("name", package->Name());
			fWorker->PostMessage(&request);
			break;
		}

		case kMsgMarkInstall:
			if (!fTransactionActive)
				_MarkSelected(kMarkInstall);
			break;

		case kMsgMarkRemove:
			if (!fTransactionActive)
				_MarkSelected(kMarkRemove);
			break;

		case kMsgMarkPurge:
			if (!fTransactionActive)
				_MarkSelected(kMarkPurge);
			break;

		case kMsgUnmark:
			if (!fTransactionActive)
				_MarkSelected(kMarkNone);
			break;

		case kMsgClearMarks:
			_ClearMarks(message);
			break;

		case kMsgAptUpdate:
			if (!fTransactionActive) {
				_SetTransactionActive(true);
				fWorker->PostMessage(kMsgAptUpdate);
			}
			break;

		case kMsgApply:
			_ApplyChanges();
			break;

		case kMsgCancel:
			if (fTransactionActive) {
				fWorker->PostMessage(kMsgCancel);
				fCancelItem->SetEnabled(false);
			}
			break;

		case kMsgRefreshList:
		{
			// Only this path warns: the startup and post-transaction
			// refreshes must consume marks silently.
			int32 marked = _CountMarked();
			if (marked > 0 && !fTransactionActive) {
				BString text(B_TRANSLATE(
					"There are %count% marked changes that have not been "
					"applied. Refreshing now will discard them."));
				BString countText;
				countText << marked;
				text.ReplaceFirst("%count%", countText);
				BAlert* alert = new BAlert(B_TRANSLATE("Package manager"),
					text.String(), B_TRANSLATE("Cancel"),
					B_TRANSLATE("Refresh anyway"), NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->SetShortcut(0, B_ESCAPE);
				if (alert->Go() != 1)
					break;
			}
			fWorker->PostMessage(kMsgRefreshList);
			break;
		}

		case kMsgReloadLog:
			fWorker->PostMessage(kMsgLoadLog);
			break;

		case kMsgMarkAllUpgrades:
		{
			if (fTransactionActive)
				break;

			for (int32 k = 0; k < fPackages.CountItems(); k++) {
				PackageInfo* package = fPackages.ItemAt(k);
				if (package->State() != kPackageUpgradable)
					continue;
				package->SetMark(kMarkInstall);
				fListView->RefreshRow(package);
			}
			_UpdatePendingUI();
			break;
		}

		case kMsgGlyphMarkChanged:
			_UpdatePendingUI();
			break;

		case kMsgManageRepositories:
			break;

		case kMsgSimulateReady:
			_HandleSimulateReady(message);
			break;

		case kMsgListReady:
			_HandleListReady(message);
			break;

		case kMsgDetailsReady:
			fInfoView->SetDetails(message);
			break;

		case kMsgChangelogReady:
			fInfoView->SetChangelog(message);
			break;

		case kMsgLogReady:
		{
			const char* text = "";
			message->FindString("text", &text);
			fLogView->SetLogText(text);
			break;
		}

		case kMsgProgress:
		{
			int32 percent = -1;
			const char* status = "";
			const char* package = "";
			message->FindInt32("percent", &percent);
			message->FindString("status", &status);
			message->FindString("package", &package);
			fStatusView->SetProgress(percent, status, package);
			break;
		}

		case kMsgTransactionDone:
			_SetTransactionActive(false);
			fStatusView->SetIdle(B_TRANSLATE("Ready"));
			fWorker->PostMessage(kMsgRefreshList);
			break;

		case kMsgError:
			_HandleError(message);
			break;

		case B_NODE_MONITOR:
			if (!fTransactionActive)
				_ShowStaleHint(true);
			break;

		default:
			Inherited::MessageReceived(message);
			break;
	}
}


bool
MainWindow::QuitRequested()
{
	if (!_RunExitGuard())
		return false;

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


// One pass when a menu opens, not one per mark change; the disabled
// entries double as a readout of pending work.
void
MainWindow::MenusBeginning()
{
	int32 upgrades = 0, installs = 0, removals = 0, purges = 0;
	for (int32 i = 0; i < fPackages.CountItems(); i++) {
		PackageInfo* package = fPackages.ItemAt(i);
		switch (package->Mark()) {
			case kMarkInstall:
				if (is_upgrade_mark(package->Mark(), package->State()))
					upgrades++;
				else
					installs++;
				break;
			case kMarkRemove:	removals++; break;
			case kMarkPurge:	purges++; break;
			default:			break;
		}
	}

	fClearMarksItem->SetEnabled(!fTransactionActive);
	const int32 counts[] = { upgrades + installs + removals + purges, upgrades,
		installs, removals, purges };
	for (int32 i = 0; i < fClearMarksMenu->CountItems(); i++)
		fClearMarksMenu->ItemAt(i)->SetEnabled(counts[i] > 0);
}


static const char*
clear_marks_label(int32 scope)
{
	switch (scope) {
		case kClearUpgradeMarks:	return B_TRANSLATE("Upgrades");
		case kClearInstallMarks:	return B_TRANSLATE("Installs");
		case kClearRemoveMarks:		return B_TRANSLATE("Removals");
		case kClearPurgeMarks:		return B_TRANSLATE("Purges");
		default:					return B_TRANSLATE("All");
	}
}


void
MainWindow::_BuildMenuBar()
{
	fMenuBar = new BMenuBar("menu bar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fRefreshItem = new BMenuItem(B_TRANSLATE("Refresh package list"),
		new BMessage(kMsgRefreshList), 'R');
	fMarkAllUpgradesItem = new BMenuItem(B_TRANSLATE("Mark all upgrades"),
		new BMessage(kMsgMarkAllUpgrades), 'U');
	fApplyItem = new BMenuItem(B_TRANSLATE("Apply changes"),
		new BMessage(kMsgApply), 'P');
	fCancelItem = new BMenuItem(B_TRANSLATE("Cancel (after current step)"),
		new BMessage(kMsgCancel));
	fCancelItem->SetEnabled(false);
	fileMenu->AddItem(fRefreshItem);
	fileMenu->AddItem(fMarkAllUpgradesItem);
	fileMenu->AddItem(fApplyItem);
	fileMenu->AddItem(fCancelItem);
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	fMenuBar->AddItem(fileMenu);

	BMenu* packageMenu = new BMenu(B_TRANSLATE("Package"));
	packageMenu->AddItem(new BMenuItem(B_TRANSLATE("Install"),
		new BMessage(kMsgMarkInstall)));
	packageMenu->AddItem(new BMenuItem(B_TRANSLATE("Remove"),
		new BMessage(kMsgMarkRemove)));
	packageMenu->AddItem(new BMenuItem(B_TRANSLATE("Purge"),
		new BMessage(kMsgMarkPurge)));
	packageMenu->AddSeparatorItem();
	packageMenu->AddItem(new BMenuItem(B_TRANSLATE("Clear mark"),
		new BMessage(kMsgUnmark)));
	// Item order mirrors clear_marks_scope.
	const int32 scopes[] = { kClearAllMarks, kClearUpgradeMarks,
		kClearInstallMarks, kClearRemoveMarks, kClearPurgeMarks };
	fClearMarksMenu = new BMenu(B_TRANSLATE("Clear marks"));
	for (uint32 i = 0; i < sizeof(scopes) / sizeof(scopes[0]); i++) {
		BMessage* message = new BMessage(kMsgClearMarks);
		message->AddInt32("which", scopes[i]);
		fClearMarksMenu->AddItem(new BMenuItem(
			clear_marks_label(scopes[i]), message));
	}
	fClearMarksItem = new BMenuItem(fClearMarksMenu);
	packageMenu->AddItem(fClearMarksItem);
	fMenuBar->AddItem(packageMenu);

	BMenu* repoMenu = new BMenu(B_TRANSLATE("Repositories"));
	BMenuItem* manageRepositoriesItem = new BMenuItem(
		B_TRANSLATE("Manage repositories"),
		new BMessage(kMsgManageRepositories));
	manageRepositoriesItem->SetEnabled(false);
	repoMenu->AddItem(manageRepositoriesItem);
	repoMenu->AddItem(new BMenuItem(B_TRANSLATE("Update package lists"),
		new BMessage(kMsgAptUpdate)));
	fMenuBar->AddItem(repoMenu);

	fApplyItem->SetEnabled(false);
}


void
MainWindow::_BuildLayout()
{
	fFilterView = new FilterView();
	fListView = new PackageListView();
	fInfoView = new PackageInfoView();
	fLogView = new AptLogView();
	fStatusView = new WorkStatusView();

	fStaleHintView = new TruncatingStringView("stale hint",
		B_TRANSLATE("The installed package list may have changed outside "
			"this window \xE2\x80\x94 use Refresh package list to update it."));
	fStaleHintView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStaleHintView->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
	fStaleHintView->Hide();

	BSplitView* splitView = new BSplitView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(splitView)
		.Add(fListView, 3.0f)
		.Add(fInfoView, 2.0f);

	fMainTabView = new BTabView("main tabs", B_WIDTH_FROM_LABEL);
	fMainTabView->AddTab(splitView);
	fMainTabView->TabAt(0)->SetLabel(B_TRANSLATE("Packages"));
	fMainTabView->AddTab(fLogView);
	fMainTabView->TabAt(1)->SetLabel(B_TRANSLATE("Log"));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0.0f)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fFilterView)
			.Add(fStaleHintView)
			.Add(fMainTabView)
			.Add(fStatusView)
		.End();
}


void
MainWindow::_ApplyFilter()
{
	fListView->SetPackages(fPackages, fFilterView->SearchText(),
		fFilterView->Section(), fFilterView->Status());
}


void
MainWindow::_MarkSelected(package_mark mark)
{
	if (fListView->MarkSelected(mark) == 0)
		return;

	_UpdatePendingUI();
}


void
MainWindow::_ClearMarks(BMessage* message)
{
	if (fTransactionActive)
		return;

	int32 scope = kClearAllMarks;
	message->FindInt32("which", &scope);

	for (int32 i = 0; i < fPackages.CountItems(); i++) {
		PackageInfo* package = fPackages.ItemAt(i);
		package_mark mark = package->Mark();
		bool clear;
		switch (scope) {
			case kClearUpgradeMarks:
				clear = is_upgrade_mark(mark, package->State());
				break;
			case kClearInstallMarks:
				clear = mark == kMarkInstall
					&& !is_upgrade_mark(mark, package->State());
				break;
			case kClearRemoveMarks:
				clear = mark == kMarkRemove;
				break;
			case kClearPurgeMarks:
				clear = mark == kMarkPurge;
				break;
			default:
				clear = mark != kMarkNone;
				break;
		}
		if (!clear)
			continue;

		package->SetMark(kMarkNone);
		fListView->RefreshRow(package);
	}

	_UpdatePendingUI();
}


void
MainWindow::_UpdatePendingUI()
{
	int32 count = _CountMarked();
	bool enabled = !fTransactionActive && count > 0;

	fApplyItem->SetEnabled(enabled);
	fInfoView->SetApplyState(count, enabled);
	fInfoView->RefreshSelectButton();
	fStatusView->SetPendingCount(count);
}


int32
MainWindow::_CountMarked() const
{
	int32 count = 0;
	for (int32 i = 0; i < fPackages.CountItems(); i++) {
		if (fPackages.ItemAt(i)->Mark() != kMarkNone)
			count++;
	}
	return count;
}


void
MainWindow::_ApplyChanges()
{
	if (fTransactionActive)
		return;

	BMessage request(kMsgSimulate);
	int32 total = 0;
	int32 installCount = 0, removeCount = 0, purgeCount = 0;

	for (int32 i = 0; i < fPackages.CountItems(); i++) {
		PackageInfo* package = fPackages.ItemAt(i);
		const char* field = NULL;
		int32* groupCount = NULL;
		switch (package->Mark()) {
			case kMarkInstall:	field = "install"; groupCount = &installCount; break;
			case kMarkRemove:	field = "remove"; groupCount = &removeCount; break;
			case kMarkPurge:	field = "purge"; groupCount = &purgeCount; break;
			default:			continue;
		}

		BString name(package->Name());
		if (package->Architecture().Length() > 0)
			name << ":" << package->Architecture();
		request.AddString(field, name);
		total++;
		(*groupCount)++;
	}

	if (total == 0)
		return;

	// Mirrors the per-verb-group bound vos-apt-helper enforces.
	if (installCount > kMaxTransactionBatchSize
		|| removeCount > kMaxTransactionBatchSize
		|| purgeCount > kMaxTransactionBatchSize) {
		BString text(B_TRANSLATE(
			"Too many packages are marked for a single action "
			"(more than %max%). Apply changes in smaller batches."));
		BString maxText;
		maxText << kMaxTransactionBatchSize;
		text.ReplaceFirst("%max%", maxText);
		BAlert* alert = new BAlert(B_TRANSLATE("Package manager"),
			text.String(), B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}

	_SetTransactionActive(true);
	fWorker->PostMessage(&request);
}


static const int32 kMaxAlertSummaryLines = 40;


// Case-insensitive on the name, architecture as tiebreaker so multiarch
// pairs stay adjacent. Pre-sorting also keeps the list view's
// binary-search inserts appending at the tail rather than memmoving
// through 69k rows.
static int
compare_packages(const PackageInfo* a, const PackageInfo* b)
{
	int result = strcasecmp(a->Name().String(), b->Name().String());
	if (result == 0)
		result = strcmp(a->Architecture().String(), b->Architecture().String());
	return result;
}


static BString
cap_summary(const BString& summary)
{
	int32 lineCount = 0;
	int32 cutAt = -1;
	int32 start = 0;
	while (start <= summary.Length()) {
		int32 newline = summary.FindFirst('\n', start);
		bool isLast = newline < 0;
		lineCount++;
		if (lineCount == kMaxAlertSummaryLines)
			cutAt = isLast ? summary.Length() : newline;
		if (isLast)
			break;
		start = newline + 1;
	}

	if (lineCount <= kMaxAlertSummaryLines || cutAt < 0)
		return summary;

	BString capped;
	summary.CopyInto(capped, 0, cutAt);
	capped << "\n... " << (lineCount - kMaxAlertSummaryLines)
		<< " more line(s) not shown.";
	return capped;
}


void
MainWindow::_HandleSimulateReady(BMessage* message)
{
	const char* summary = "";
	message->FindString("summary", &summary);

	BString text(B_TRANSLATE("Apply these changes?"));
	text << "\n\n" << cap_summary(BString(summary));

	BAlert* alert = new BAlert(B_TRANSLATE("Package manager"), text.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Apply"), NULL, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1) {
		_SetTransactionActive(false);
		fStatusView->SetIdle(B_TRANSLATE("Ready"));
		return;
	}

	BMessage apply(kMsgApplyChanges);
	const char* name = NULL;
	for (int32 i = 0; message->FindString("install", i, &name) == B_OK; i++)
		apply.AddString("install", name);
	for (int32 i = 0; message->FindString("remove", i, &name) == B_OK; i++)
		apply.AddString("remove", name);
	for (int32 i = 0; message->FindString("purge", i, &name) == B_OK; i++)
		apply.AddString("purge", name);
	fWorker->PostMessage(&apply);
}


void
MainWindow::_SetTransactionActive(bool active)
{
	fTransactionActive = active;

	fRefreshItem->SetEnabled(!active);
	fMarkAllUpgradesItem->SetEnabled(!active);
	fCancelItem->SetEnabled(active);
	fListView->SetTransactionActive(active);
	fInfoView->SetTransactionActive(active);
	_UpdatePendingUI();
}


void
MainWindow::_HandleListReady(BMessage* message)
{
	BObjectList<PackageInfo>* incoming = NULL;
	if (message->FindPointer("packages", (void**)&incoming) != B_OK
			|| incoming == NULL) {
		return;
	}

	// Rows and the info view borrow these pointers; clear them first.
	fListView->ClearPackages();
	fInfoView->SetPackage(NULL);

	fPackages.MakeEmpty();
	for (int32 i = 0; i < incoming->CountItems(); i++)
		fPackages.AddItem(incoming->ItemAt(i));
	incoming->MakeEmpty();
	delete incoming;

	fPackages.SortItems(compare_packages);

	BObjectList<BString, true> sections(32);
	for (int32 i = 0; i < fPackages.CountItems(); i++) {
		const BString& category = fPackages.ItemAt(i)->Category();
		if (category.Length() == 0)
			continue;
		bool seen = false;
		for (int32 j = 0; j < sections.CountItems() && !seen; j++)
			seen = (*sections.ItemAt(j) == category);
		if (!seen)
			sections.AddItem(new BString(category));
	}
	fFilterView->SetSections(sections);

	_ApplyFilter();
	_UpdatePendingUI();
	fStatusView->SetIdle(B_TRANSLATE("Ready"));
	_ShowStaleHint(false);
}


void
MainWindow::_HandleError(BMessage* message)
{
	int32 error = kAptErrorUnknown;
	const char* detail = "";
	message->FindInt32("error", &error);
	message->FindString("detail", &detail);

	_SetTransactionActive(false);

	BString text;
	switch (error) {
		case kAptErrorLockHeld:
			text = B_TRANSLATE("Another program is using the package "
				"system. Close it and try again.");
			break;
		case kAptErrorNoPrivilege:
			text = B_TRANSLATE("Authentication failed or was cancelled. "
				"Nothing was changed.");
			break;
		case kAptErrorBrokenDeps:
			text = B_TRANSLATE("These changes cannot be applied without "
				"breaking other packages.");
			break;
		case kAptErrorNetwork:
			text = B_TRANSLATE("Could not reach the software repositories.");
			break;
		case kAptErrorSignature:
			text = B_TRANSLATE("A repository failed its signature check. "
				"Nothing was installed.");
			break;
		case kAptErrorDiskFull:
			text = B_TRANSLATE("There is not enough free disk space.");
			break;
		case kAptErrorInterrupted:
			text = B_TRANSLATE("The operation was interrupted.");
			break;
		case kAptErrorBatchTooLarge:
			text = B_TRANSLATE("Too many packages are marked for a single "
				"action. Apply changes in smaller batches.");
			break;
		default:
			text = B_TRANSLATE("The operation failed.");
			break;
	}

	if (detail != NULL && detail[0] != '\0')
		text << "\n\n" << detail;

	BAlert* alert = new BAlert(B_TRANSLATE("Package manager"), text.String(),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


bool
MainWindow::_RunExitGuard()
{
	// The transaction is a pkexec'd root apt-get that cannot be stopped
	// safely; killing it half-configures packages, so refuse to quit.
	if (fTransactionActive) {
		BAlert* alert = new BAlert(B_TRANSLATE("Quit"),
			B_TRANSLATE("A package operation is running as root right now. "
				"Quitting will not stop it. Wait for it to finish before "
				"closing Package Manager."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return false;
	}

	// Marks are in-memory only, but easy to forget; offer a way back.
	if (_CountMarked() > 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Quit"),
			B_TRANSLATE("There are marked changes that have not been "
				"applied. Quitting now will discard them."),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Quit anyway"), NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 0)
			return false;
	}

	return true;
}


void
MainWindow::_WatchDpkgStatus()
{
	BEntry entry("/var/lib/dpkg/status");
	node_ref nodeRef;
	if (entry.Exists() && entry.GetNodeRef(&nodeRef) == B_OK)
		watch_node(&nodeRef, B_WATCH_STAT, BMessenger(this));
}


void
MainWindow::_ShowStaleHint(bool show)
{
	bool isHidden = fStaleHintView->IsHidden();
	if (show && isHidden)
		fStaleHintView->Show();
	else if (!show && !isHidden)
		fStaleHintView->Hide();
}
