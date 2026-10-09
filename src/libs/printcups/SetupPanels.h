/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SETUP_PANELS_H
#define _SETUP_PANELS_H


#include <Message.h>
#include <Rect.h>
#include <Window.h>


const uint32 kMsgPaperSelected		= 'PapS';
const uint32 kMsgOrientationChanged	= 'OrnC';
const uint32 kMsgPageSetupOk		= 'PSOk';
const uint32 kMsgPageSetupCancel	= 'PSCa';
const uint32 kMsgJobSetupOk			= 'JSOk';
const uint32 kMsgJobSetupCancel		= 'JSCa';


class PageSetupWindow : public BWindow {
public:
						PageSetupWindow(BMessage* settings, bool* canceled,
							sem_id done);

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

private:
	void				_UpdateSettings();
	void				_UpdatePreview();
	void				_Finish(bool canceled);
	void				_Release(bool canceled);

	BMessage*		fSettings;
	bool*			fCanceled;
	sem_id			fDone;
	bool			fFinished;
	int32			fOrientation;
	class BPopUpMenu*	fPaperMenu;
	class BPopUpMenu*	fOrientationMenu;
	class PreviewView*	fPreview;
};


class JobSetupWindow : public BWindow {
public:
						JobSetupWindow(BMessage* settings, bool* canceled,
							sem_id done);

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

private:
	void				_UpdateSettings();
	void				_Finish(bool canceled);
	void				_Release(bool canceled);

	BMessage*		fSettings;
	bool*			fCanceled;
	sem_id			fDone;
	bool			fFinished;
	class BTextControl*	fCopiesField;
	class BTextControl*	fFirstField;
	class BTextControl*	fLastField;
};


#endif // _SETUP_PANELS_H
