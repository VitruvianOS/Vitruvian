/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "InfoWin.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>
#include <StringView.h>
#include <stdio.h>

#include "Controller.h"
#include "DurationToString.h"

#include <storage/Mime.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaPlayer"


// Draws the node's large icon in the header row.
class IconView : public BView {
public:
								IconView(const entry_ref& ref);
	virtual						~IconView();

	virtual	void				Draw(BRect updateRect);

private:
			BBitmap*			fIcon;
};


IconView::IconView(const entry_ref& ref)
	:
	BView(BRect(0, 0, 31, 31), "icon", B_FOLLOW_NONE, B_WILL_DRAW),
	fIcon(NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return;
	BNodeInfo nodeInfo(&node);
	if (nodeInfo.InitCheck() != B_OK)
		return;
	fIcon = new BBitmap(BRect(0, 0, 31, 31), B_RGB32);
	if (fIcon->InitCheck() != B_OK) {
		delete fIcon;
		fIcon = NULL;
		return;
	}
	if (nodeInfo.GetIcon(fIcon, B_LARGE_ICON) != B_OK) {
		delete fIcon;
		fIcon = NULL;
	}
}


IconView::~IconView()
{
	delete fIcon;
}


void
IconView::Draw(BRect updateRect)
{
	if (fIcon != NULL)
		DrawBitmap(fIcon, Bounds());
}


static BStringView*
_CreateLabel(const char* text)
{
	BStringView* label = new BStringView(NULL, text);
	label->SetFont(be_bold_font);
	return label;
}


InfoWin::InfoWin(BRect frame, Controller* controller, const entry_ref& ref)
	:
	BWindow(frame, B_TRANSLATE("File Info"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fName(NULL),
	fContainer(NULL),
	fLocation(NULL),
	fDuration(NULL),
	fVideo(NULL),
	fAudio(NULL),
	fCopyright(NULL)
{
	_Setup(controller, ref);
	Show();
}


InfoWin::~InfoWin()
{
}


void
InfoWin::_Setup(Controller* controller, const entry_ref& ref)
{
	BPath path(&ref);
	BString location(path.Path());

	BString container("?");
	BNode node(&ref);
	if (node.InitCheck() == B_OK) {
		BNodeInfo nodeInfo(&node);
		char type[B_MIME_TYPE_LENGTH];
		if (nodeInfo.InitCheck() == B_OK
				&& nodeInfo.GetType(type) == B_OK)
			container = type;
	}

	BString durationText("-");
	bigtime_t duration = controller->Duration();
	if (duration > 0) {
		char buf[64];
		duration_to_string((int32)(duration / 100000), buf, sizeof(buf));
		durationText = buf;
	}

	BString videoText;
	if (controller->HasVideo()) {
		videoText << controller->VideoWidth() << "x"
			<< controller->VideoHeight();
		if (controller->VideoFrameRate() > 0.0f) {
			char fps[32];
			snprintf(fps, sizeof(fps), ", %.2f fps",
				controller->VideoFrameRate());
			videoText << fps;
		}
		if (controller->VideoCodec()[0] != '\0')
			videoText << ", " << controller->VideoCodec();
	}

	BString audioText;
	if (controller->AudioChannels() > 0) {
		if (controller->AudioCodec()[0] != '\0')
			audioText << controller->AudioCodec() << ", ";
		if (controller->AudioFrameRate() > 0.0f)
			audioText << (int32)controller->AudioFrameRate() << " Hz, ";
		audioText << controller->AudioChannels() << " ch";
		if (controller->AudioBits() > 0)
			audioText << ", " << controller->AudioBits() << "-bit";
	}

	BString copyright;
	char copyrightBuf[256];
	ssize_t size = node.ReadAttr("Media:copyright", B_STRING_TYPE, 0,
		copyrightBuf, sizeof(copyrightBuf) - 1);
	if (size > 0) {
		copyrightBuf[size] = '\0';
		copyright = copyrightBuf;
	}

	fName = new BStringView(NULL, ref.name);
	fName->SetFont(be_bold_font);
	fContainer = new BStringView(NULL, container.String());
	fLocation = new BStringView(NULL, location.String());
	fDuration = new BStringView(NULL, durationText.String());
	fVideo = new BStringView(NULL, videoText.String());
	fAudio = new BStringView(NULL, audioText.String());
	fCopyright = new BStringView(NULL, copyright.String());

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(10)
		.AddGroup(B_HORIZONTAL, 8)
			.Add(new IconView(ref))
			.Add(fName)
		.End()
		.AddGrid(0.0, 6)
			.Add(_CreateLabel(B_TRANSLATE("Container:")), 0, 0)
			.Add(fContainer, 1, 0)
			.Add(_CreateLabel(B_TRANSLATE("Location:")), 0, 1)
			.Add(fLocation, 1, 1)
			.Add(_CreateLabel(B_TRANSLATE("Duration:")), 0, 2)
			.Add(fDuration, 1, 2)
			.Add(_CreateLabel(B_TRANSLATE("Video:")), 0, 3)
			.Add(fVideo, 1, 3)
			.Add(_CreateLabel(B_TRANSLATE("Audio:")), 0, 4)
			.Add(fAudio, 1, 4)
			.Add(_CreateLabel(B_TRANSLATE("Copyright:")), 0, 5)
			.Add(fCopyright, 1, 5)
		.End();

	ResizeTo(Bounds().Width(), Bounds().Height());
	CenterOnScreen();
}
