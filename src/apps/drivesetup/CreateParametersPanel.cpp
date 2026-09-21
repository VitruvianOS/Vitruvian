/*
 * Copyright 2008-2013 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de.
 *		Bryce Groff	<bgroff@hawaii.edu>
 *		Karsten Heimrich <host.haiku@gmx.de>
 */


#include "CreateParametersPanel.h"

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <DiskDeviceTypes.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Variant.h>

#include "Support.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "CreateParametersPanel"


enum {
	MSG_PARTITION_TYPE			= 'type',
	MSG_SIZE_SLIDER				= 'ssld',
	MSG_SIZE_TEXTCONTROL		= 'stct'
};

static const uint32 kMegaByte = 0x100000;

// Standard CHS compatibility geometry; same default as fdisk/GParted.
static const off_t kCylinderSize = 255LL * 63 * 512;


CreateParametersPanel::CreateParametersPanel(BWindow* window,
	BPartition* partition, off_t offset, off_t size)
	:
	ChangeParametersPanel(window)
{
	_CreateCreateControls(partition, offset, size);

	Init(B_CREATE_PARAMETER_EDITOR, "", partition);
}


CreateParametersPanel::~CreateParametersPanel()
{
}


status_t
CreateParametersPanel::Go(off_t& offset, off_t& size, BString& name,
	BString& type, BString& parameters)
{
	// The object will be deleted in Go(), so we need to get the values via
	// a BMessage

	BMessage storage;
	status_t status = ChangeParametersPanel::Go(name, type, parameters,
		storage);
	if (status != B_OK)
		return status;

	// Return the value back as bytes.
	size = storage.GetInt64("size", 0);
	offset = storage.GetInt64("offset", 0);

	return B_OK;
}


void
CreateParametersPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SIZE_SLIDER:
			_UpdateSizeTextControl();
			break;

		case MSG_SIZE_TEXTCONTROL:
		{
			off_t size = strtoll(fSizeTextControl->Text(), NULL, 10) * kMegaByte;
			if (size >= 0 && size <= fSizeSlider->MaxPartitionSize())
				fSizeSlider->SetSize(size);
			else
				_UpdateSizeTextControl();
			break;
		}

		default:
			ChangeParametersPanel::MessageReceived(message);
	}
}


bool
CreateParametersPanel::NeedsEditor() const
{
	return false;
}


status_t
CreateParametersPanel::ParametersReceived(const BString& parameters,
	BMessage& storage)
{
	off_t offset = fSizeSlider->Offset();
	off_t size = fSizeSlider->Size();

	off_t alignedOffset = _AlignOffset(offset);
	if (alignedOffset > offset) {
		off_t delta = alignedOffset - offset;
		size -= delta;
		if (size < 0)
			size = 0;
		offset = alignedOffset;
	}

	// Return the value back as bytes.
	status_t status = storage.SetInt64("size", size);
	if (status == B_OK)
		status = storage.SetInt64("offset", offset);

	if (status != B_OK)
		return status;

	return ChangeParametersPanel::ParametersReceived(parameters, storage);
}


void
CreateParametersPanel::AddControls(BLayoutBuilder::Group<>& builder,
	BView* editorView)
{
	builder
		.Add(fSizeSlider)
		.Add(fSizeTextControl)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(new BStringView("alignment label",
				B_TRANSLATE("Start alignment:")))
			.Add(fAlign1MiBRadio)
			.Add(fAlign4MiBRadio)
			.Add(fAlignCylinderRadio)
			.Add(fAlignNoneRadio)
		.End();

	ChangeParametersPanel::AddControls(builder, editorView);
}


void
CreateParametersPanel::_CreateCreateControls(BPartition* parent, off_t offset,
	off_t size)
{
	// Setup the controls
	// TODO: use a lower granularity for smaller disks -- but this would
	// require being able to parse arbitrary size strings with unit
	fSizeSlider = new SizeSlider("Slider", B_TRANSLATE("Partition size"), NULL,
		offset, size, kMegaByte);
	fSizeSlider->SetPosition(1.0);
	fSizeSlider->SetModificationMessage(new BMessage(MSG_SIZE_SLIDER));

	fSizeTextControl = new BTextControl("Size Control", "", "", NULL);
	for(int32 i = 0; i < 256; i++)
		fSizeTextControl->TextView()->DisallowChar(i);
	for(int32 i = '0'; i <= '9'; i++)
		fSizeTextControl->TextView()->AllowChar(i);
	_UpdateSizeTextControl();
	fSizeTextControl->SetModificationMessage(
		new BMessage(MSG_SIZE_TEXTCONTROL));

	fAlign1MiBRadio = new BRadioButton("align1MiB",
		B_TRANSLATE("1 MiB boundary"), NULL);
	fAlign1MiBRadio->SetValue(B_CONTROL_ON);
	fAlign4MiBRadio = new BRadioButton("align4MiB",
		B_TRANSLATE("4 MiB boundary"), NULL);
	fAlignCylinderRadio = new BRadioButton("alignCylinder",
		B_TRANSLATE("Cylinder boundary (legacy)"), NULL);
	fAlignNoneRadio = new BRadioButton("alignNone",
		B_TRANSLATE("None"), NULL);

	CreateChangeControls(NULL, parent);

	fOkButton->SetLabel(B_TRANSLATE("Create"));
}


void
CreateParametersPanel::_UpdateSizeTextControl()
{
	BString sizeString;
	sizeString << fSizeSlider->Size() / kMegaByte;
	fSizeTextControl->SetText(sizeString.String());
}


off_t
CreateParametersPanel::_AlignmentGranularity() const
{
	if (fAlign4MiBRadio->Value() == B_CONTROL_ON)
		return 4 * off_t(kMegaByte);
	if (fAlignCylinderRadio->Value() == B_CONTROL_ON)
		return kCylinderSize;
	if (fAlignNoneRadio->Value() == B_CONTROL_ON)
		return 0;
	return off_t(kMegaByte);
}


off_t
CreateParametersPanel::_AlignOffset(off_t offset) const
{
	off_t granularity = _AlignmentGranularity();
	if (granularity <= 0)
		return offset;

	off_t remainder = offset % granularity;
	if (remainder == 0)
		return offset;

	return offset + (granularity - remainder);
}
