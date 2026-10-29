/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "RenderingView.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextView.h>

#include <GLRendererPriority.h>
#include <opengl/GlDeviceSelect.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


static const uint32 kMsgMoveUp = 'hwUp';
static const uint32 kMsgMoveDown = 'hwDn';
static const uint32 kMsgReset = 'hwRs';
static const uint32 kMsgSelectionChanged = 'hwSc';
static const uint32 kMsgPolicyChanged = 'hwPc';


RenderingView::RenderingView()
	:
	BView("rendering", B_WILL_DRAW),
	fListView(NULL),
	fUpButton(NULL),
	fDownButton(NULL),
	fResetButton(NULL),
	fPolicyField(NULL),
	fStatusLabel(NULL),
	fStatusView(NULL)
{
	fListView = new BListView("renderers", B_SINGLE_SELECTION_LIST);
	fListView->SetSelectionMessage(new BMessage(kMsgSelectionChanged));

	BScrollView* scroll = new BScrollView("scroll", fListView,
		B_FRAME_EVENTS, false, true);

	fUpButton = new BButton("up", B_TRANSLATE("Up"),
		new BMessage(kMsgMoveUp));
	fDownButton = new BButton("down", B_TRANSLATE("Down"),
		new BMessage(kMsgMoveDown));
	fResetButton = new BButton("reset", B_TRANSLATE("Reset to add-on order"),
		new BMessage(kMsgReset));

	BMenu* policyMenu = new BMenu(B_TRANSLATE("GPU"));
	policyMenu->SetLabelFromMarked(true);
	static const char* kPolicyValues[3]
		= { "automatic", "power_saving", "high_performance" };
	static const char* kPolicyLabels[3] = {
		B_TRANSLATE("Automatic"),
		B_TRANSLATE("Power saving (display GPU)"),
		B_TRANSLATE("High performance (discrete GPU)")
	};
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kMsgPolicyChanged);
		message->AddString("value", kPolicyValues[i]);
		policyMenu->AddItem(new BMenuItem(kPolicyLabels[i], message));
	}
	fPolicyField = new BMenuField("policy", B_TRANSLATE("GPU policy"),
		policyMenu);
	fPolicyField->Menu()->SetTargetForItems(this);

	fStatusView = new BTextView("gpustatus");
	fStatusView->MakeEditable(false);
	fStatusView->MakeSelectable(false);
	BScrollView* statusScroll = new BScrollView("statusscroll",
		fStatusView, B_FRAME_EVENTS, false, true);
	fStatusView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fStatusLabel = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(scroll)
		.AddGrid()
			.Add(fUpButton, 0, 0)
			.Add(fDownButton, 1, 0)
			.Add(fResetButton, 2, 0)
		.End()
		.Add(fPolicyField)
		.Add(statusScroll)
		.Add(fStatusLabel)
		.SetInsets(B_USE_DEFAULT_SPACING);
}


RenderingView::~RenderingView()
{
}


RenderingView*
RenderingView::Create()
{
	return new RenderingView();
}


void
RenderingView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fUpButton->SetTarget(this);
	fDownButton->SetTarget(this);
	fResetButton->SetTarget(this);
	fListView->SetTarget(this);

	_LoadList();
	_UpdateButtons();
	_LoadPolicy();
	_RefreshStatus();
}


void
RenderingView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgMoveUp:
			_Move(-1);
			break;
		case kMsgMoveDown:
			_Move(1);
			break;
		case kMsgReset:
			// Back to raw add-on directory order, then persist it so
			// the file no longer overrides anything.
			fPriority.MakeEmpty();
			GLRendererPriority::GetInstalledRenderers(fPriority);
			_PopulateList();
			_SaveList();
			_UpdateButtons();
			break;
		case kMsgSelectionChanged:
			_UpdateButtons();
			break;
		case kMsgPolicyChanged:
			_SavePolicy();
			_RefreshStatus();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
RenderingView::_LoadList()
{
	fPriority.MakeEmpty();
	GLRendererPriority::GetEffective(fPriority);
	_PopulateList();

	if (fListView->CountItems() > 0) {
		fStatusLabel->SetText(B_TRANSLATE(
			"Use Up and Down to reorder. Changes are saved immediately."));
	} else {
		fStatusLabel->SetText(B_TRANSLATE(
			"No OpenGL renderer add-ons are installed."));
	}
}


void
RenderingView::_PopulateList()
{
	fListView->MakeEmpty();
	for (int32 index = 0; index < fPriority.CountStrings(); index++)
		fListView->AddItem(new BStringItem(fPriority.StringAt(index)));

	if (fListView->CountItems() > 0)
		fListView->Select(0);
}


void
RenderingView::_SaveList()
{
	// Persist exactly what is shown, top (most preferred) first.
	fPriority.MakeEmpty();
	for (int32 index = 0; index < fListView->CountItems(); index++) {
		BStringItem* item = static_cast<BStringItem*>(
			fListView->ItemAt(index));
		if (item != NULL)
			fPriority.Add(BString(item->Text()));
	}

	status_t status = GLRendererPriority::Save(fPriority);
	if (status == B_OK) {
		fStatusLabel->SetText(B_TRANSLATE(
			"Order saved. It applies the next time a GL view is created."));
	} else {
		fStatusLabel->SetText(B_TRANSLATE("Saving the order failed."));
	}
}


void
RenderingView::_UpdateButtons()
{
	int32 index = fListView->CurrentSelection();
	int32 count = fListView->CountItems();

	fUpButton->SetEnabled(index > 0);
	fDownButton->SetEnabled(index >= 0 && index < count - 1);
}


status_t
RenderingView::_Move(int32 offset)
{
	int32 index = fListView->CurrentSelection();
	int32 target = index + offset;
	if (index < 0 || target < 0 || target >= fListView->CountItems())
		return B_BAD_VALUE;

	BStringItem* item
		= static_cast<BStringItem*>(fListView->RemoveItem(index));
	if (item == NULL)
		return B_ERROR;
	fListView->AddItem(item, target);
	fListView->Select(target);

	_SaveList();
	return B_OK;
}


void
RenderingView::_LoadPolicy()
{
	int policy = kGlPolicyAutomatic;
	gl_load_policy(&policy);

	const char* value = "automatic";
	if (policy == kGlPolicyPowerSaving)
		value = "power_saving";
	else if (policy == kGlPolicyHighPerformance)
		value = "high_performance";

	BMenu* menu = fPolicyField->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message() != NULL
				&& strcmp(item->Message()->GetString("value", ""),
					value) == 0) {
			item->SetMarked(true);
			break;
		}
	}
}


void
RenderingView::_SavePolicy()
{
	BMenuItem* marked = fPolicyField->Menu()->FindMarked();
	if (marked == NULL || marked->Message() == NULL)
		return;

	const char* value = marked->Message()->GetString("value", "automatic");
	int policy = kGlPolicyAutomatic;
	if (strcmp(value, "power_saving") == 0)
		policy = kGlPolicyPowerSaving;
	else if (strcmp(value, "high_performance") == 0)
		policy = kGlPolicyHighPerformance;

	if (gl_save_policy(policy) != B_OK)
		fStatusLabel->SetText(B_TRANSLATE("Saving the GPU policy failed."));
}


void
RenderingView::_RefreshStatus()
{
	gl_device devices[16];
	int count = gl_enumerate_devices(devices, 16);
	if (count <= 0) {
		fStatusView->SetText(B_TRANSLATE(
			"GPU status: no DRM devices found."));
		return;
	}

	BString text;
	char line[768];
	for (int i = 0; i < count; i++) {
		const char* node = devices[i].renderNode[0] != '\0'
			? devices[i].renderNode : devices[i].primaryNode;
		char status[512];
		if (gl_read_gl_status(node, status, sizeof(status))) {
			// Published by the renderer: glRenderer|zinkWhy|deviceWhy.
			char renderer[256], zinkWhy[64], deviceWhy[128];
			renderer[0] = zinkWhy[0] = deviceWhy[0] = '\0';
			sscanf(status, "%255[^|]|%63[^|]|%127[^\n]", renderer,
				zinkWhy, deviceWhy);
			// "zink fallback" reads better as "automatic fallback".
			if (strcmp(zinkWhy, "zink fallback") == 0)
				snprintf(zinkWhy, sizeof(zinkWhy), "automatic fallback");
			snprintf(line, sizeof(line),
				"%s (%s): OpenGL: %s (%s); %s\n", node,
				devices[i].driver, renderer, zinkWhy, deviceWhy);
		} else {
			snprintf(line, sizeof(line),
				"%s (%s): no GL client has reported yet.\n",
				node, devices[i].driver);
		}
		text << line;
	}

	fStatusView->SetText(text.String());
}
