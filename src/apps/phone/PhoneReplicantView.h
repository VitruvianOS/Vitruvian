/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _PHONE_REPLICANT_VIEW_H
#define _PHONE_REPLICANT_VIEW_H


#include <String.h>
#include <View.h>


class BTelephonyRoster;


// Replicant form of Phone: a one-line strip sized for the Deskbar tray
// that also works on the desktop shelf. A click opens the dialer; the
// dragger moves it between the shelf and the Deskbar.
class PhoneReplicantView : public BView {
public:
								PhoneReplicantView(BRect frame,
									int32 resizingMode, bool inDeskbar);
								PhoneReplicantView(BMessage* archive);

	virtual						~PhoneReplicantView();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				GetPreferredSize(float* _width,
									float* _height);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Init(bool inDeskbar);
			void				_SyncFromRoster();
			void				_OpenDialer();

	static const char*			kAppSignature;

			BTelephonyRoster*	fRoster;
			bool				fInDeskbar;
			BString				fStatus;
};


#endif // _PHONE_REPLICANT_VIEW_H
