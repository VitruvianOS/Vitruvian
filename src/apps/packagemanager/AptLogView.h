/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef APT_LOG_VIEW_H
#define APT_LOG_VIEW_H

#include <GroupView.h>


class BButton;
class BTextView;


class AptLogView : public BGroupView {
	typedef BGroupView Inherited;
public:
								AptLogView();
	virtual						~AptLogView();

	virtual	void				AttachedToWindow();

			void				SetLogText(const char* text);

private:
			BTextView*			fTextView;
			BButton*			fReloadButton;
};


#endif // APT_LOG_VIEW_H
