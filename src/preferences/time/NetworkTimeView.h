/*
 * Copyright 2011-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Hamish Morrison, hamish@lavabit.com
 *		John Scipione, jscipione@gmail.com
 */
#ifndef NETWORK_TIME_VIEW_H
#define NETWORK_TIME_VIEW_H


#include <LayoutBuilder.h>


class BCheckBox;
class BMessage;
class BStringView;


class NetworkTimeView : public BGroupView {
public:
							NetworkTimeView(const char* name);
	virtual					~NetworkTimeView();

	virtual	void			MessageReceived(BMessage* message);
	virtual	void			AttachedToWindow();

			bool			CheckCanRevert();

private:
			void			_InitView();
			void			_StartLoadState();
			void			_UpdateStatus();
			void			_NotifyNTPChanged();
			void			_ApplyNTP(bool enable);

			BCheckBox*		fNTPCheckBox;
			BStringView*	fStatusView;

			bool			fNTPEnabled;
			bool			fNTPSynced;
			bool			fNTPOpPending;
};


#endif	// NETWORK_TIME_VIEW_H
