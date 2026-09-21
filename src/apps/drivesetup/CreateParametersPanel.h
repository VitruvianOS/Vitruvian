/*
 * Copyright 2008-2013 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de.
 *		Bryce Groff	  <bgroff@hawaii.edu>
 */
#ifndef CREATE_PARAMS_PANEL_H
#define CREATE_PARAMS_PANEL_H


#include "ChangeParametersPanel.h"


class SizeSlider;
class BRadioButton;


// Start alignment options; 1 MiB default, others for GParted parity.
enum alignment_mode {
	ALIGN_1_MIB = 0,
	ALIGN_4_MIB,
	ALIGN_CYLINDER,
	ALIGN_NONE
};


class CreateParametersPanel : public ChangeParametersPanel {
public:
								CreateParametersPanel(BWindow* window,
									BPartition* parent, off_t offset,
									off_t size);
	virtual						~CreateParametersPanel();

			status_t			Go(off_t& offset, off_t& size, BString& name,
									BString& type, BString& parameters);

	virtual	void				MessageReceived(BMessage* message);

protected:
	virtual bool				NeedsEditor() const;
	virtual status_t			ParametersReceived(const BString& parameters,
									BMessage& storage);
	virtual	void				AddControls(BLayoutBuilder::Group<>& builder,
									BView* editorView);

private:
			void 				_CreateCreateControls(BPartition* parent,
									off_t offset, off_t size);

			void				_UpdateSizeTextControl();

			off_t				_AlignmentGranularity() const;
			off_t				_AlignOffset(off_t offset) const;

private:
			SizeSlider*			fSizeSlider;
			BTextControl*		fSizeTextControl;

			BRadioButton*		fAlign1MiBRadio;
			BRadioButton*		fAlign4MiBRadio;
			BRadioButton*		fAlignCylinderRadio;
			BRadioButton*		fAlignNoneRadio;
};


#endif // CREATE_PARAMS_PANEL_H
