/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ServicesWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <RadioButton.h>
#include <Size.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "ServicesList.h"
#include "ServicesWorker.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ServicesWindow"


static const BRect kDefaultFrame(80, 80, 920, 680);

// Cap preferred width of detail/status text so a long unit description
// or journal line cannot force the window wider than this.
static const float kMaxDetailWidth = 420;


ServicesWindow::ServicesWindow(BRect frame)
	:
	Inherited(frame, B_TRANSLATE_SYSTEM_NAME("Services"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fUnitFilter(kFilterServicesOnly),
	fUsesUser(false),
	fLoading(false)
{
	if (!frame.IsValid() || frame.Width() < 500 || frame.Height() < 360)
		frame = kDefaultFrame;

	_BuildMenuBar();
	_BuildLayout();

	fWorker = new ServicesWorker(BMessenger(this));
	fWorker->Run();

	fLoading = true;
	BMessage connect(kMsgSetManager);
	connect.AddString("manager", "system");
	fWorker->PostMessage(&connect);

	MoveOnScreen();
}


ServicesWindow::~ServicesWindow()
{
	if (fWorker != NULL) {
		BMessage shutdown(kMsgShutdown);
		fWorker->PostMessage(&shutdown);
	}
}


bool
ServicesWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
ServicesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFilterChanged:
			_ApplyFilter();
			break;

		case kMsgAllUnitsToggled:
			fUnitFilter = fAllUnitsCheck->Value() == B_CONTROL_ON
				? kFilterAllUnits : kFilterServicesOnly;
			_ApplyFilter();
			break;

		case kMsgManagerChanged:
			_SwitchManager();
			break;

		case kMsgUnitSelected:
			_ShowSelectedDetail();
			_UpdateActionButtons();
			break;

		case kMsgStart:
		case kMsgStop:
		case kMsgRestart:
		case kMsgEnable:
		case kMsgDisable:
			_PostUnitAction(message->what);
			break;

		case kMsgUnitsBatch:
			_HandleUnitsBatch(message);
			break;

		case kMsgUnitsReady:
			_HandleUnitsReady(message);
			break;

		case kMsgUnitUpdated:
			_HandleUnitUpdated(message);
			break;

		case kMsgUnitRemoved:
			_HandleUnitRemoved(message);
			break;

		case kMsgJournalReady:
			_HandleJournal(message);
			break;

		case kMsgActionDone:
			_HandleActionDone(message);
			break;

		case kMsgError:
			_ShowError(message);
			break;

		default:
			Inherited::MessageReceived(message);
			break;
	}
}


void
ServicesWindow::_BuildMenuBar()
{
	fMenuBar = new BMenuBar("menu bar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fRefreshItem = new BMenuItem(B_TRANSLATE("Reload units"),
		new BMessage(kMsgReload), 'R');
	fileMenu->AddItem(fRefreshItem);
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	fMenuBar->AddItem(fileMenu);

	BMenu* managerMenu = new BMenu(B_TRANSLATE("Manager"));
	managerMenu->SetRadioMode(true);
	managerMenu->SetLabelFromMarked(true);
	fSystemItem = new BMenuItem(B_TRANSLATE("System"),
		new BMessage(kMsgManagerChanged));
	fUserItem = new BMenuItem(B_TRANSLATE("User"),
		new BMessage(kMsgManagerChanged));
	fSystemItem->SetMarked(true);
	managerMenu->AddItem(fSystemItem);
	managerMenu->AddItem(fUserItem);
	fMenuBar->AddItem(managerMenu);

	BMenu* serviceMenu = new BMenu(B_TRANSLATE("Service"));
	serviceMenu->AddItem(new BMenuItem(B_TRANSLATE("Start"),
		new BMessage(kMsgStart)));
	serviceMenu->AddItem(new BMenuItem(B_TRANSLATE("Stop"),
		new BMessage(kMsgStop)));
	serviceMenu->AddItem(new BMenuItem(B_TRANSLATE("Restart"),
		new BMessage(kMsgRestart)));
	serviceMenu->AddSeparatorItem();
	serviceMenu->AddItem(new BMenuItem(B_TRANSLATE("Enable"),
		new BMessage(kMsgEnable)));
	serviceMenu->AddItem(new BMenuItem(B_TRANSLATE("Disable"),
		new BMessage(kMsgDisable)));
	fMenuBar->AddItem(serviceMenu);

	BMenu* helpMenu = new BMenu(B_TRANSLATE("Help"));
	helpMenu->AddItem(new BMenuItem(
		B_TRANSLATE("About Services" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED)));
	fMenuBar->AddItem(helpMenu);
}


void
ServicesWindow::_BuildLayout()
{
	fSystemRadio = new BRadioButton("system manager",
		B_TRANSLATE("System"), new BMessage(kMsgManagerChanged));
	fUserRadio = new BRadioButton("user manager",
		B_TRANSLATE("User"), new BMessage(kMsgManagerChanged));
	fSystemRadio->SetValue(B_CONTROL_ON);

	fAllUnitsCheck = new BCheckBox("all units",
		B_TRANSLATE("All units"), new BMessage(kMsgAllUnitsToggled));

	fFilterControl = new BTextControl("filter", B_TRANSLATE("Filter:"), "",
		new BMessage(kMsgFilterChanged));
	fFilterControl->SetDivider(be_plain_font->StringWidth(
		B_TRANSLATE("Filter:")) + 10);

	fStartButton = new BButton("start", B_TRANSLATE("Start"),
		new BMessage(kMsgStart));
	fStopButton = new BButton("stop", B_TRANSLATE("Stop"),
		new BMessage(kMsgStop));
	fRestartButton = new BButton("restart", B_TRANSLATE("Restart"),
		new BMessage(kMsgRestart));
	fEnableButton = new BButton("enable", B_TRANSLATE("Enable"),
		new BMessage(kMsgEnable));
	fDisableButton = new BButton("disable", B_TRANSLATE("Disable"),
		new BMessage(kMsgDisable));

	fList = new ServicesList();
	fList->SetExplicitMinSize(BSize(300, 200));

	fDetailTitle = new BStringView("detail title",
		B_TRANSLATE("No unit selected"));
	fDetailTitle->SetFont(be_bold_font);
	fDetailState = new BStringView("detail state", "");
	// Preferred width tracks the text; without a cap a long description
	// widens the whole layout.
	fDetailTitle->SetExplicitMaxSize(BSize(kMaxDetailWidth,
		B_SIZE_UNLIMITED));
	fDetailState->SetExplicitMaxSize(BSize(kMaxDetailWidth,
		B_SIZE_UNLIMITED));

	fJournalView = new BTextView("journal",
		B_WILL_DRAW | B_FRAME_EVENTS);
	// Read-only journal text.
	fJournalView->MakeEditable(false);
	// B_SIZE_UNLIMITED as a min width is FLT_MAX and blows the window up.
	fJournalView->SetExplicitMinSize(BSize(0, 120));
	fJournalView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		B_SIZE_UNLIMITED));

	fStatusLabel = new BStringView("status", B_TRANSLATE("Ready"));
	fStatusLabel->SetExplicitMaxSize(BSize(600, B_SIZE_UNLIMITED));

	BSplitView* split = new BSplitView(B_VERTICAL, B_USE_SMALL_SPACING);

	BView* detail = new BView("detail", 0);
	BLayoutBuilder::Group<>(detail, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fDetailTitle)
		.Add(fDetailState)
		.Add(fJournalView, 1.0f);

	BLayoutBuilder::Split<>(split)
		.Add(fList, 3.0f)
		.Add(detail, 2.0f);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(fSystemRadio)
			.Add(fUserRadio)
			.AddGlue()
			.Add(fAllUnitsCheck)
			.Add(fFilterControl, 1.0f)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(fStartButton)
			.Add(fStopButton)
			.Add(fRestartButton)
			.Add(fEnableButton)
			.Add(fDisableButton)
			.AddGlue()
		.End()
		.Add(split, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(fStatusLabel, 1.0f)
		.End();

	fFilterControl->MakeFocus(true);
}


void
ServicesWindow::_SwitchManager()
{
	bool user = fUserRadio->Value() == B_CONTROL_ON
		|| (fMenuBar != NULL && fUserItem != NULL
			&& fUserItem->IsMarked());

	fUsesUser = user;
	if (fSystemRadio != NULL && fUserRadio != NULL) {
		if (user) {
			fUserRadio->SetValue(B_CONTROL_ON);
			fSystemRadio->SetValue(B_CONTROL_OFF);
		} else {
			fSystemRadio->SetValue(B_CONTROL_ON);
			fUserRadio->SetValue(B_CONTROL_OFF);
		}
	}
	if (fSystemItem != NULL && fUserItem != NULL) {
		if (user)
			fUserItem->SetMarked(true);
		else
			fSystemItem->SetMarked(true);
	}

	fUnits.MakeEmpty();
	fLoading = true;
	_RebuildList();
	fJournalView->SetText(B_EMPTY_STRING);
	fDetailTitle->SetText(B_TRANSLATE("No unit selected"));
	fDetailState->SetText(B_EMPTY_STRING);
	_UpdateActionButtons();
	_SetStatus(user
		? B_TRANSLATE("Switching to user manager...")
		: B_TRANSLATE("Switching to system manager..."));

	BMessage connect(kMsgSetManager);
	connect.AddString("manager", user ? "user" : "system");
	fWorker->PostMessage(&connect);
}


void
ServicesWindow::_ApplyFilter()
{
	fFilterText = fFilterControl->Text();
	_RebuildList();
}


void
ServicesWindow::_RebuildList()
{
	fList->SetUnits(fUnits, fFilterText, fUnitFilter);
}


void
ServicesWindow::_ShowSelectedDetail()
{
	ServicesUnit unit = fList->SelectedUnit();
	if (unit.fName.Length() == 0) {
		fDetailTitle->SetText(B_TRANSLATE("No unit selected"));
		fDetailState->SetText(B_EMPTY_STRING);
		fJournalView->SetText(B_EMPTY_STRING);
		return;
	}

	BString title = unit.fName;
	if (unit.fDescription.Length() > 0)
		title << ": " << unit.fDescription;
	fDetailTitle->SetText(title.String());

	BString state;
	BString enabled = unit.fUnitFileState.Length() > 0
		? unit.fUnitFileState : BString(B_TRANSLATE("(unknown)"));
	state << B_TRANSLATE("Load:") << " " << unit.fLoadState
		<< "   " << B_TRANSLATE("Active:") << " " << unit.fActiveState
		<< "   " << B_TRANSLATE("Sub:") << " " << unit.fSubState
		<< "   " << B_TRANSLATE("Enabled:") << " " << enabled;
	fDetailState->SetText(state.String());

	BMessage journal(kMsgLoadJournal);
	journal.AddString("unit", unit.fName.String());
	journal.AddBool("user", fUsesUser);
	fWorker->PostMessage(&journal);
}


void
ServicesWindow::_PostUnitAction(uint32 what)
{
	ServicesUnit unit = fList->SelectedUnit();
	if (unit.fName.Length() == 0) {
		_SetStatus(B_TRANSLATE("Select a unit first."));
		return;
	}

	BMessage request;
	switch (what) {
		case kMsgStart:		request.what = kMsgStartUnit; break;
		case kMsgStop:		request.what = kMsgStopUnit; break;
		case kMsgRestart:	request.what = kMsgRestartUnit; break;
		case kMsgEnable:	request.what = kMsgEnableUnit; break;
		case kMsgDisable:	request.what = kMsgDisableUnit; break;
		default:
			return;
	}
	request.AddString("unit", unit.fName.String());
	fWorker->PostMessage(&request);

	BString status;
	status << B_TRANSLATE("Sent request to systemd: ") << unit.fName;
	_SetStatus(status.String());
}


void
ServicesWindow::_SetStatus(const char* text)
{
	fStatusLabel->SetText(text != NULL ? text : "");
}


void
ServicesWindow::_ShowError(BMessage* message)
{
	if (message->GetBool("cancelled", false)) {
		_SetStatus(B_TRANSLATE("Ready"));
		return;
	}

	const char* operation = "";
	const char* detail = "";
	message->FindString("operation", &operation);
	message->FindString("detail", &detail);

	// Denied / access errors are expected for unprivileged system units;
	// show them in the window rather than crashing or ignoring them.
	BString status;
	status << B_TRANSLATE("Error") << " (" << operation << "): " << detail;
	_SetStatus(status.String());

	BString alertText = status;
	BAlert* alert = new BAlert("services error", alertText.String(),
		B_TRANSLATE("OK"));
	alert->Go();
}


void
ServicesWindow::_HandleUnitsBatch(BMessage* message)
{
	BObjectList<ServicesUnit, true>* incoming = NULL;
	if (message->FindPointer("units", (void**)&incoming) != B_OK
			|| incoming == NULL)
		return;

	for (int32 i = 0; i < incoming->CountItems(); i++) {
		ServicesUnit* unit = incoming->ItemAt(i);
		if (unit == NULL)
			continue;
		fUnits.AddItem(new ServicesUnit(*unit));
		fList->UpdateUnit(*unit);
	}
	incoming->MakeEmpty();
	delete incoming;

	int32 total = 0;
	message->FindInt32("total", &total);
	BString status;
	status << B_TRANSLATE("Loading units:") << " " << fUnits.CountItems();
	if (total > 0)
		status << " / " << total;
	_SetStatus(status.String());
}


void
ServicesWindow::_HandleUnitsReady(BMessage* message)
{
	bool user = false;
	message->FindBool("user", &user);
	fUsesUser = user;

	// Final pass so any filter change mid-load is applied once.
	fLoading = false;
	_RebuildList();
	_UpdateActionButtons();

	int32 count = 0;
	message->FindInt32("count", &count);
	if (count <= 0)
		count = fUnits.CountItems();
	BString status;
	status << B_TRANSLATE("Units:") << " " << count
		<< "  (" << (user ? B_TRANSLATE("user") : B_TRANSLATE("system"))
		<< B_TRANSLATE(" manager)");
	_SetStatus(status.String());
}


void
ServicesWindow::_HandleUnitUpdated(BMessage* message)
{
	ServicesUnit unit;
	if (!ServicesUnit::FromMessage(message, &unit))
		return;

	for (int32 i = 0; i < fUnits.CountItems(); i++) {
		ServicesUnit* existing = fUnits.ItemAt(i);
		if (existing != NULL && existing->fName == unit.fName) {
			*existing = unit;
			if (!fLoading)
				fList->UpdateUnit(unit);
			return;
		}
	}

	fUnits.AddItem(new ServicesUnit(unit));
	if (fLoading)
		return;

	// Only touch the visible list for units the filter accepts; a
	// non-matching unit must not trigger a full rebuild.
	if (unit.MatchesFilter(fUnitFilter) && unit.MatchesText(fFilterText))
		fList->UpdateUnit(unit);

	_UpdateActionButtons();
}


void
ServicesWindow::_HandleUnitRemoved(BMessage* message)
{
	const char* name = "";
	if (message->FindString("unit", &name) != B_OK || name == NULL)
		return;

	for (int32 i = 0; i < fUnits.CountItems(); i++) {
		ServicesUnit* existing = fUnits.ItemAt(i);
		if (existing != NULL && existing->fName == name) {
			fUnits.RemoveItem(existing);
			break;
		}
	}
	fList->RemoveUnit(name);
	_UpdateActionButtons();
}


void
ServicesWindow::_HandleJournal(BMessage* message)
{
	const char* text = "";
	message->FindString("text", &text);
	fJournalView->SetText(text != NULL ? text : "");
}


void
ServicesWindow::_HandleActionDone(BMessage* message)
{
	const char* unit = "";
	const char* verb = "";
	message->FindString("unit", &unit);
	message->FindString("verb", &verb);

	BString status;
	status << B_TRANSLATE("Done: ") << verb << " " << unit;
	_SetStatus(status.String());

	// Selection may have changed active state; refresh the detail pane.
	_ShowSelectedDetail();
}


void
ServicesWindow::_UpdateActionButtons()
{
	bool has = fList->HasSelection();
	fStartButton->SetEnabled(has);
	fStopButton->SetEnabled(has);
	fRestartButton->SetEnabled(has);
	fEnableButton->SetEnabled(has);
	fDisableButton->SetEnabled(has);
	if (fRefreshItem != NULL)
		fRefreshItem->SetEnabled(true);
}
