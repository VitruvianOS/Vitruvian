/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SETUP_PANELS_H
#define _SETUP_PANELS_H


#include <Message.h>
#include <Rect.h>
#include <String.h>
#include <Window.h>


const uint32 kMsgPaperSelected		= 'PapS';
const uint32 kMsgOrientationChanged	= 'OrnC';
const uint32 kMsgPageSetupOk		= 'PSOk';
const uint32 kMsgPageSetupCancel	= 'PSCa';
const uint32 kMsgJobSetupOk			= 'JSOk';
const uint32 kMsgJobSetupCancel		= 'JSCa';
const uint32 kMsgJobSetupPrinter	= 'JSPr';
const uint32 kMsgJobSetupChoose		= 'JSCh';

// BMessage string: where a job for the PDF printer is written.
#define PRINTCUPS_FIELD_SAVE_PATH "save_path"

// Optional hints an application can put in the settings it hands to
// ConfigPage(), so the page setup reflects the document being printed:
//  - PRINTCUPS_FIELD_CONTENT_RECT: a BRect with the size of the document's
//    page. Used to pick portrait or landscape the first time, when the
//    settings carry no orientation yet.
//  - PRINTCUPS_FIELD_PREVIEW: an archived BBitmap of the page, drawn inside
//    the preview's printable area.
#define PRINTCUPS_FIELD_CONTENT_RECT "printcups:content"
#define PRINTCUPS_FIELD_PREVIEW "printcups:preview"


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
	virtual				~JobSetupWindow();

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

private:
	void				_UpdateSettings();
	void				_Finish(bool canceled);
	void				_Release(bool canceled);
	bool				_PdfPrinterSelected() const;
	void				_UpdateSaveControls();
	BString				_DefaultSavePath() const;
	void				_ChooseFile();

	BMessage*		fSettings;
	bool*			fCanceled;
	sem_id			fDone;
	bool			fFinished;
	class BPopUpMenu*	fPrinterMenu;
	class BTextControl*	fSaveField;
	class BButton*		fChooseButton;
	class BFilePanel*	fSavePanel;
	class BTextControl*	fCopiesField;
	class BTextControl*	fFirstField;
	class BTextControl*	fLastField;
};


#endif // _SETUP_PANELS_H
