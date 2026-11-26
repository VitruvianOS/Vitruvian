/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ACCESSIBILITY_REPLICANT_H
#define ACCESSIBILITY_REPLICANT_H


#include <Bitmap.h>
#include <String.h>
#include <View.h>


class AccessibilityReplicant : public BView {
public:
								AccessibilityReplicant(BRect frame,
									int32 resizingMode,
									bool inDeskbar = false);
								AccessibilityReplicant(BMessage* archive);
	virtual						~AccessibilityReplicant();

	static	BView*				Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive, bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				Pulse();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);

private:
			void				_SetLabel(const char* label);

			BBitmap*			fIcon;
			BString				fLabel;
			bool				fInDeskbar;
};


#endif	// ACCESSIBILITY_REPLICANT_H
