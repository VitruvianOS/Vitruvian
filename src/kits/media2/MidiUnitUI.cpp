/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/NativeMidiUnits.h>

#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include <Size.h>

#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <media2/MediaConnection.h>


static const uint32 kMsgDrainMonitor = 'Mdrn';
static const uint32 kMsgTogglePause = 'Mpau';
static const uint32 kMsgClearMonitor = 'Mclr';
static const uint32 kMsgKeyboardChanged = 'Kbch';
static const uint32 kMsgKeyboardRefresh = 'Kbrf';


static int32
_ClampInt(int32 value, int32 minValue, int32 maxValue)
{
	if (value < minValue)
		return minValue;
	if (value > maxValue)
		return maxValue;
	return value;
}


static const char* const kOnOffItems[] = { "Off", "On" };


static const char* const kChannelNames[] = {
	"1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15", "16"
};


static const char*
_MidiEventName(uint8 type)
{
	switch (type) {
		case B_MIDI_NOTE_OFF:		return "Note Off";
		case B_MIDI_NOTE_ON:		return "Note On";
		case B_MIDI_KEY_PRESSURE:	return "Key Pressure";
		case B_MIDI_CONTROL_CHANGE:	return "Control Change";
		case B_MIDI_PROGRAM_CHANGE:	return "Program Change";
		case B_MIDI_CHANNEL_PRESSURE: return "Ch Pressure";
		case B_MIDI_PITCH_BEND:		return "Pitch Bend";
		case B_MIDI_SYSTEM_EXCLUSIVE: return "SysEx";
		case B_MIDI_SYSTEM_COMMON:	return "System Common";
		case B_MIDI_SYSTEM_REAL_TIME: return "Real Time";
		default:					return "Unknown";
	}
}


// #pragma mark - keyboard window


class BMidiKeyboardView;


class BMidiKeyboardWindow : public BWindow {
public:
								BMidiKeyboardWindow(BMidiKeyboardUnit* unit,
									const BRect& frame);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			void				RefreshFromUnit();

private:
			BMidiKeyboardUnit*	fUnit;
			BMidiKeyboardView*	fView;
			BMenuField*			fChannelField;
			BMenuField*			fOctaveField;
			BSlider*			fVelocitySlider;
};


// Two octaves of piano keys, played with the mouse or the computer
// keyboard (z..m lower row, q..u upper row, black keys on s/d/g/h/j and
// 2/3/5/6/7).
class BMidiKeyboardView : public BView {
public:
								BMidiKeyboardView(BMidiKeyboardUnit* unit);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				KeyUp(const char* bytes, int32 numBytes);
	virtual	void				WindowActivated(bool active);
	virtual	void				AllAttached();

private:
			int32				_KeyAt(BPoint where) const;
			void				_KeyDown(int32 note);
			void				_KeyUp(int32 note);
			void				_ReleaseAll();

			BMidiKeyboardUnit*	fUnit;
			int32				fMouseDownNote;
			bool				fMouseButtonDown;
			bool				fKeyDown[24];
};


BMidiKeyboardView::BMidiKeyboardView(BMidiKeyboardUnit* unit)
	:
	BView("piano", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
		| B_FRAME_EVENTS | B_NAVIGABLE),
	fUnit(unit),
	fMouseDownNote(-1),
	fMouseButtonDown(false)
{
	memset(fKeyDown, 0, sizeof(fKeyDown));
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
BMidiKeyboardView::AllAttached()
{
	BView::AllAttached();
	SetExplicitMinSize(BSize(280, 72));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 72));
}


int32
BMidiKeyboardView::_KeyAt(BPoint where) const
{
	// 14 white keys, two octaves; black keys overhang the seams.
	const float whiteWidth = Bounds().Width() / 14.0f;
	static const int32 kWhiteSemis[] = { 0, 2, 4, 5, 7, 9, 11 };
	static const int32 kBlackSemis[] = { 1, 3, 6, 8, 10 };

	int32 white = (int32)(where.x / whiteWidth);
	if (white < 0 || white >= 14)
		return -1;

	// Black keys first: they sit on top of the seam between whites.
	float blackWidth = whiteWidth * 0.62f;
	for (int32 i = 0; i < 5; i++) {
		int32 whiteAfter = i < 2 ? i : i + 1;
		float seamX = (whiteAfter + 1) * whiteWidth - blackWidth / 2.0f;
		if (where.x >= seamX && where.x < seamX + blackWidth
				&& where.y < Bounds().Height() * 0.62f) {
			int32 octave = i < 2 ? 0 : 1;
			return octave * 12 + kBlackSemis[i % 5 == 4 ? 4 : (i % 5)];
		}
	}

	int32 octave = white / 7;
	int32 index = white % 7;
	return octave * 12 + kWhiteSemis[index];
}


void
BMidiKeyboardView::Draw(BRect updateRect)
{
	const BRect bounds = Bounds();
	const float whiteWidth = bounds.Width() / 14.0f;
	const float height = bounds.Height();
	static const int32 kWhiteSemis[] = { 0, 2, 4, 5, 7, 9, 11 };
	static const int32 kBlackSemis[] = { 1, 3, 6, 8, 10 };

	rgb_color white = { 255, 255, 255, 255 };
	rgb_color black = { 20, 20, 20, 255 };
	rgb_color played = { 51, 102, 204, 255 };
	rgb_color border = { 120, 120, 120, 255 };

	for (int32 i = 0; i < 14; i++) {
		int32 semis = (i / 7) * 12 + kWhiteSemis[i % 7];
		BRect key(i * whiteWidth, 0, (i + 1) * whiteWidth - 1, height - 1);
		bool down = semis < 24 && fKeyDown[semis];
		SetHighColor(down ? played : white);
		FillRect(key);
		SetHighColor(border);
		StrokeRect(key);
	}

	float blackWidth = whiteWidth * 0.62f;
	float blackHeight = height * 0.62f;
	for (int32 i = 0; i < 10; i++) {
		int32 octave = i / 5;
		int32 inOctave = i % 5;
		int32 semis = octave * 12 + kBlackSemis[inOctave];
		// Seam after white index 0,1 then 3,4,5.
		static const int32 kAfterWhite[] = { 0, 1, 3, 4, 5 };
		float seamX = (octave * 7 + kAfterWhite[inOctave] + 1) * whiteWidth
			- blackWidth / 2.0f;
		BRect key(seamX, 0, seamX + blackWidth - 1, blackHeight - 1);
		bool down = semis < 24 && fKeyDown[semis];
		SetHighColor(down ? played : black);
		FillRect(key);
		SetHighColor(border);
		StrokeRect(key);
	}
}


void
BMidiKeyboardView::MouseDown(BPoint where)
{
	MakeFocus();
	fMouseButtonDown = true;
	fMouseDownNote = _KeyAt(where);
	if (fMouseDownNote >= 0)
		_KeyDown(fMouseDownNote);
}


void
BMidiKeyboardView::MouseUp(BPoint where)
{
	where = where;
	fMouseButtonDown = false;
	if (fMouseDownNote >= 0) {
		_KeyUp(fMouseDownNote);
		fMouseDownNote = -1;
	}
}


void
BMidiKeyboardView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	dragMessage = dragMessage;
	if (fMouseDownNote < 0 || !fMouseButtonDown)
		return;

	int32 note = _KeyAt(where);
	if (note == fMouseDownNote)
		return;
	// Glissando: release the old key, grab the new one.
	_KeyUp(fMouseDownNote);
	fMouseDownNote = note;
	if (note >= 0)
		_KeyDown(note);
}


void
BMidiKeyboardView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes < 1 || fUnit == NULL)
		return;

	int32 base = 12 * (fUnit->Octave() + 1);
	int32 semis = -1;
	switch (bytes[0]) {
		case 'z': semis = 0; break;
		case 's': semis = 1; break;
		case 'x': semis = 2; break;
		case 'd': semis = 3; break;
		case 'c': semis = 4; break;
		case 'v': semis = 5; break;
		case 'g': semis = 6; break;
		case 'b': semis = 7; break;
		case 'h': semis = 8; break;
		case 'n': semis = 9; break;
		case 'j': semis = 10; break;
		case 'm': semis = 11; break;
		case 'q': semis = 12; break;
		case '2': semis = 13; break;
		case 'w': semis = 14; break;
		case '3': semis = 15; break;
		case 'e': semis = 16; break;
		case 'r': semis = 17; break;
		case '5': semis = 18; break;
		case 't': semis = 19; break;
		case '6': semis = 20; break;
		case 'y': semis = 21; break;
		case '7': semis = 22; break;
		case 'u': semis = 23; break;
		default:
			BView::KeyDown(bytes, numBytes);
			return;
	}

	if (semis < 0 || base + semis > 127)
		return;
	_KeyDown(semis);
	Invalidate();
}


void
BMidiKeyboardView::KeyUp(const char* bytes, int32 numBytes)
{
	if (numBytes < 1) {
		BView::KeyUp(bytes, numBytes);
		return;
	}

	int32 semis = -1;
	switch (bytes[0]) {
		case 'z': semis = 0; break;
		case 's': semis = 1; break;
		case 'x': semis = 2; break;
		case 'd': semis = 3; break;
		case 'c': semis = 4; break;
		case 'v': semis = 5; break;
		case 'g': semis = 6; break;
		case 'b': semis = 7; break;
		case 'h': semis = 8; break;
		case 'n': semis = 9; break;
		case 'j': semis = 10; break;
		case 'm': semis = 11; break;
		case 'q': semis = 12; break;
		case '2': semis = 13; break;
		case 'w': semis = 14; break;
		case '3': semis = 15; break;
		case 'e': semis = 16; break;
		case 'r': semis = 17; break;
		case '5': semis = 18; break;
		case 't': semis = 19; break;
		case '6': semis = 20; break;
		case 'y': semis = 21; break;
		case '7': semis = 22; break;
		case 'u': semis = 23; break;
		default:
			BView::KeyUp(bytes, numBytes);
			return;
	}

	if (semis >= 0) {
		_KeyUp(semis);
		Invalidate();
	}
}


void
BMidiKeyboardView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
	if (!active)
		_ReleaseAll();
}


void
BMidiKeyboardView::_KeyDown(int32 semis)
{
	if (semis < 0 || semis >= 24 || fKeyDown[semis] || fUnit == NULL)
		return;
	fKeyDown[semis] = true;
	int32 note = 12 * (fUnit->Octave() + 1) + semis;
	if (note <= 127)
		fUnit->PlayNote((uchar)note);
}


void
BMidiKeyboardView::_KeyUp(int32 semis)
{
	if (semis < 0 || semis >= 24 || !fKeyDown[semis] || fUnit == NULL)
		return;
	fKeyDown[semis] = false;
	int32 note = 12 * (fUnit->Octave() + 1) + semis;
	if (note <= 127)
		fUnit->StopNote((uchar)note);
}


void
BMidiKeyboardView::_ReleaseAll()
{
	if (fUnit != NULL)
		fUnit->ReleaseAll();
	memset(fKeyDown, 0, sizeof(fKeyDown));
	Invalidate();
}


BMidiKeyboardWindow::BMidiKeyboardWindow(BMidiKeyboardUnit* unit,
	const BRect& frame)
	:
	BWindow(frame, "Virtual Keyboard", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
	fUnit(unit),
	fView(NULL),
	fChannelField(NULL),
	fOctaveField(NULL),
	fVelocitySlider(NULL)
{
	fView = new(std::nothrow) BMidiKeyboardView(unit);
	if (fView == NULL) {
		return;
	}

	BPopUpMenu* channelMenu = new(std::nothrow) BPopUpMenu("channel");
	BPopUpMenu* octaveMenu = new(std::nothrow) BPopUpMenu("octave");
	if (channelMenu == NULL || octaveMenu == NULL) {
		delete fView;
		return;
	}
	for (int32 i = 0; i < 16; i++) {
		channelMenu->AddItem(new BMenuItem(kChannelNames[i],
			new BMessage(kMsgKeyboardChanged)));
	}
	const char* kOctaveNames[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8" };
	for (int32 i = 0; i < 9; i++) {
		octaveMenu->AddItem(new BMenuItem(kOctaveNames[i],
			new BMessage(kMsgKeyboardChanged)));
	}
	channelMenu->SetTargetForItems(this);
	octaveMenu->SetTargetForItems(this);

	fChannelField = new(std::nothrow) BMenuField("channel", "Channel",
		channelMenu);
	fOctaveField = new(std::nothrow) BMenuField("octave", "Octave",
		octaveMenu);
	fVelocitySlider = new(std::nothrow) BSlider("velocity", "Velocity",
		NULL, 1, 127, B_HORIZONTAL);

	if (fChannelField == NULL || fOctaveField == NULL
			|| fVelocitySlider == NULL) {
		delete fView;
		return;
	}

	fVelocitySlider->SetModificationMessage(new BMessage(kMsgKeyboardChanged));
	fVelocitySlider->SetExplicitMinSize(BSize(140, B_SIZE_UNSET));

	BGroupView* controls = new(std::nothrow) BGroupView(B_HORIZONTAL, 0.0f);
	if (controls == NULL) {
		delete fView;
		return;
	}
	controls->AddChild(fChannelField);
	controls->AddChild(fOctaveField);
	controls->AddChild(fVelocitySlider);

	BView* content = new(std::nothrow) BGroupView(B_VERTICAL, 0.0f);
	if (content == NULL) {
		delete fView;
		return;
	}
	content->AddChild(controls);
	content->AddChild(fView);
	SetLayout(new BGroupLayout(B_VERTICAL, 0.0f));
	AddChild(content);

	RefreshFromUnit();
}


void
BMidiKeyboardWindow::RefreshFromUnit()
{
	if (fUnit == NULL || fChannelField == NULL || fOctaveField == NULL
			|| fVelocitySlider == NULL) {
		return;
	}

	BMenu* channelMenu = fChannelField->Menu();
	if (channelMenu != NULL)
		channelMenu->ItemAt(fUnit->Channel())->SetMarked(true);

	BMenu* octaveMenu = fOctaveField->Menu();
	if (octaveMenu != NULL)
		octaveMenu->ItemAt(fUnit->Octave())->SetMarked(true);

	fVelocitySlider->SetValue(fUnit->Velocity());
}


void
BMidiKeyboardWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgKeyboardChanged:
		{
			if (fUnit == NULL)
				break;

			// Menus mark their item before the message arrives.
			if (fChannelField != NULL && fChannelField->Menu() != NULL) {
				int32 index = fChannelField->Menu()->FindMarkedIndex();
				if (index >= 0)
					fUnit->SetParameter(0, (float)index);
			}
			if (fOctaveField != NULL && fOctaveField->Menu() != NULL) {
				int32 index = fOctaveField->Menu()->FindMarkedIndex();
				if (index >= 0)
					fUnit->SetParameter(1, (float)index);
			}
			if (fVelocitySlider != NULL)
				fUnit->SetParameter(2, (float)fVelocitySlider->Value());
			break;
		}
		case kMsgKeyboardRefresh:
			RefreshFromUnit();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
BMidiKeyboardWindow::QuitRequested()
{
	// The unit owns the window: closing hides it, the node keeps running.
	if (fUnit != NULL)
		fUnit->HideWindow();
	return false;
}


// #pragma mark - BMidiKeyboardUnit


BMidiKeyboardUnit::BMidiKeyboardUnit()
	:
	BMidiNativeUnit("Virtual Keyboard", B_MEDIA_PLAYER),
	fOutput(NULL),
	fWindow(NULL),
	fChannel(0),
	fOctave(4),
	fVelocity(100)
{
	memset(fKeysDown, 0, sizeof(fKeysDown));

	fOutput = new(std::nothrow) BMidiOutput("midi out");
	if (fOutput != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		fOutput->SetAcceptedFormat(format);
		if (RegisterOutput(fOutput) != B_OK) {
			delete fOutput;
			fOutput = NULL;
		}
	}
	if (fOutput == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	static const char* const kOctaveNames[] = {
		"0", "1", "2", "3", "4", "5", "6", "7", "8"
	};
	AddDiscreteParam(0, "Channel", 0, kChannelNames, 16, NULL);
	AddDiscreteParam(1, "Octave", 4, kOctaveNames, 9, NULL);
	AddContinuousParam(2, "Velocity", 1.0f, 127.0f, 100.0f);
	FinishParameters("Virtual Keyboard");
}


BMidiKeyboardUnit::~BMidiKeyboardUnit()
{
	Stop();
	if (fWindow != NULL) {
		fWindow->Lock();
		fWindow->Quit();
		fWindow = NULL;
	}
	if (fOutput != NULL) {
		UnregisterOutput(fOutput);
		delete fOutput;
	}
}


uchar
BMidiKeyboardUnit::Channel() const
{
	return (uchar)_ClampInt(fChannel.load(), 0, 15);
}


int32
BMidiKeyboardUnit::Octave() const
{
	return _ClampInt(fOctave.load(), 0, 8);
}


uchar
BMidiKeyboardUnit::Velocity() const
{
	return (uchar)_ClampInt(fVelocity.load(), 1, 127);
}


void
BMidiKeyboardUnit::PlayNote(uchar note)
{
	if (fOutput == NULL || fKeysDown[note])
		return;
	fKeysDown[note] = true;
	fOutput->SendNoteOn(Channel(), note, Velocity(), 0);
}


void
BMidiKeyboardUnit::StopNote(uchar note)
{
	if (fOutput == NULL || !fKeysDown[note])
		return;
	fKeysDown[note] = false;
	fOutput->SendNoteOff(Channel(), note, 0, 0);
}


void
BMidiKeyboardUnit::ReleaseAll()
{
	if (fOutput == NULL)
		return;
	for (int32 note = 0; note < 128; note++) {
		if (!fKeysDown[note])
			continue;
		fKeysDown[note] = false;
		fOutput->SendNoteOff(Channel(), (uchar)note, 0, 0);
	}
}


void
BMidiKeyboardUnit::ShowWindow()
{
	if (fWindow == NULL) {
		fWindow = new(std::nothrow) BMidiKeyboardWindow(this,
			BRect(150, 150, 150 + 420, 150 + 160));
		if (fWindow == NULL)
			return;
	}
	fWindow->Show();
}


void
BMidiKeyboardUnit::HideWindow()
{
	if (fWindow != NULL)
		fWindow->Hide();
}


void
BMidiKeyboardUnit::HandleStart(bigtime_t /*performanceTime*/)
{
	ShowWindow();
}


void
BMidiKeyboardUnit::HandleStop(bigtime_t performanceTime)
{
	ReleaseAll();
	if (fOutput != NULL)
		fOutput->AllNotesOff(performanceTime);
	HideWindow();
}


void
BMidiKeyboardUnit::ParameterChanged(int32 id)
{
	if (id == 0)
		fChannel.store((int32)lroundf(FloatParam(0)));
	else if (id == 1)
		fOctave.store((int32)lroundf(FloatParam(1)));
	else if (id == 2)
		fVelocity.store((int32)lroundf(FloatParam(2)));

	if (fWindow != NULL)
		fWindow->PostMessage(kMsgKeyboardRefresh);
}


// #pragma mark - monitor window


class BMidiMonitorWindow : public BWindow {
public:
								BMidiMonitorWindow(BMidiMonitorUnit* unit,
									const BRect& frame);
	virtual						~BMidiMonitorWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Drain();

			BMidiMonitorUnit*	fUnit;
			BTextView*			fLog;
			BCheckBox*			fPauseBox;
			BMessageRunner*		fRunner;
};


BMidiMonitorWindow::BMidiMonitorWindow(BMidiMonitorUnit* unit,
	const BRect& frame)
	:
	BWindow(frame, "MIDI Monitor", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fUnit(unit),
	fLog(NULL),
	fPauseBox(NULL),
	fRunner(NULL)
{
	fLog = new(std::nothrow) BTextView("log",
		B_WILL_DRAW | B_FRAME_EVENTS);
	if (fLog != NULL) {
		fLog->MakeEditable(false);
		fLog->SetWordWrap(false);
		fLog->SetFont(be_fixed_font);
	}

	BScrollView* scroller = new(std::nothrow) BScrollView("scroller", fLog,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);
	BButton* clearButton = new(std::nothrow) BButton("Clear",
		new BMessage(kMsgClearMonitor));
	fPauseBox = new(std::nothrow) BCheckBox("Paused", "Paused",
		new BMessage(kMsgTogglePause));

	BGroupView* controls = new(std::nothrow) BGroupView(B_HORIZONTAL, 0.0f);
	if (controls != NULL) {
		if (fPauseBox != NULL)
			controls->AddChild(fPauseBox);
		if (clearButton != NULL)
			controls->AddChild(clearButton);
	}

	BGroupView* content = new(std::nothrow) BGroupView(B_VERTICAL, 0.0f);
	if (content != NULL) {
		if (controls != NULL)
			content->AddChild(controls);
		if (scroller != NULL)
			content->AddChild(scroller);
	}
	SetLayout(new BGroupLayout(B_VERTICAL, 0.0f));
	if (content != NULL)
		AddChild(content);

	if (fUnit != NULL) {
		BMessage drain(kMsgDrainMonitor);
		fRunner = new(std::nothrow) BMessageRunner(BMessenger(this), &drain,
			50000);
	}
}


BMidiMonitorWindow::~BMidiMonitorWindow()
{
	delete fRunner;
}


void
BMidiMonitorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDrainMonitor:
			_Drain();
			break;
		case kMsgTogglePause:
			if (fUnit != NULL && fPauseBox != NULL)
				fUnit->SetPaused(fPauseBox->Value() == B_CONTROL_ON);
			break;
		case kMsgClearMonitor:
			if (fLog != NULL)
				fLog->Clear();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
BMidiMonitorWindow::QuitRequested()
{
	if (fUnit != NULL)
		fUnit->HideWindow();
	return false;
}


void
BMidiMonitorWindow::_Drain()
{
	if (fUnit == NULL || fLog == NULL)
		return;

	bigtime_t times[64];
	uint8 types[64];
	uchar channels[64];
	uchar data1[64];
	uchar data2[64];
	int32 count = fUnit->ReadEvents(64, times, types, channels, data1, data2);
	if (count <= 0)
		return;

	bigtime_t start = fUnit->StartTime();
	BString text;
	for (int32 i = 0; i < count; i++) {
		double seconds = (times[i] - start) / 1000000.0;
		text << "+ " << seconds << "  ch " << (int)(channels[i] + 1)
			<< "  " << _MidiEventName(types[i])
			<< "  " << (int)data1[i] << " " << (int)data2[i] << "\n";
	}

	// Keep the log bounded; drop the older half when it grows too big.
	if (fLog->TextLength() > 256 * 1024) {
		int32 cut = fLog->OffsetAt(fLog->CountLines() / 2);
		fLog->Delete(0, cut);
	}
	fLog->Insert(text.String());
	fLog->ScrollToOffset(fLog->TextLength());
}


// #pragma mark - BMidiMonitorUnit


class BMidiMonitorUnit::MonitorInput : public BMidiInput {
public:
								MonitorInput(BMidiMonitorUnit* unit);

protected:
	virtual	void				NoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				NoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				KeyPressure(uchar channel, uchar note,
									uchar pressure, bigtime_t time) override;
	virtual	void				ControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time) override;
	virtual	void				ProgramChange(uchar channel,
									uchar programNumber, bigtime_t time) override;
	virtual	void				ChannelPressure(uchar channel,
									uchar pressure, bigtime_t time) override;
	virtual	void				PitchBend(uchar channel, uchar lsb,
									uchar msb, bigtime_t time) override;
	virtual	void				SystemExclusive(void* data, size_t length,
									bigtime_t time) override;
	virtual	void				SystemCommon(uchar status, uchar data1,
									uchar data2, bigtime_t time) override;
	virtual	void				SystemRealTime(uchar status,
									bigtime_t time) override;

private:
			BMidiMonitorUnit*	fUnit;
};


BMidiMonitorUnit::MonitorInput::MonitorInput(BMidiMonitorUnit* unit)
	:
	// Virtual base must be named by the most-derived class.
	BMediaConnection(B_MEDIA_INPUT, "midi in"),
	BMidiInput("midi in"),
	fUnit(unit)
{
}


void
BMidiMonitorUnit::MonitorInput::NoteOff(uchar channel, uchar note,
	uchar velocity, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_NOTE_OFF, channel, note, velocity);
}


void
BMidiMonitorUnit::MonitorInput::NoteOn(uchar channel, uchar note,
	uchar velocity, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_NOTE_ON, channel, note, velocity);
}


void
BMidiMonitorUnit::MonitorInput::KeyPressure(uchar channel, uchar note,
	uchar pressure, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_KEY_PRESSURE, channel, note, pressure);
}


void
BMidiMonitorUnit::MonitorInput::ControlChange(uchar channel,
	uchar controlNumber, uchar controlValue, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_CONTROL_CHANGE, channel, controlNumber,
		controlValue);
}


void
BMidiMonitorUnit::MonitorInput::ProgramChange(uchar channel,
	uchar programNumber, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_PROGRAM_CHANGE, channel, programNumber, 0);
}


void
BMidiMonitorUnit::MonitorInput::ChannelPressure(uchar channel,
	uchar pressure, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_CHANNEL_PRESSURE, channel, pressure, 0);
}


void
BMidiMonitorUnit::MonitorInput::PitchBend(uchar channel, uchar lsb,
	uchar msb, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_PITCH_BEND, channel, lsb, msb);
}


void
BMidiMonitorUnit::MonitorInput::SystemExclusive(void* /*data*/,
	size_t length, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_SYSTEM_EXCLUSIVE, 0,
		(uchar)(length & 0x7f), 0);
}


void
BMidiMonitorUnit::MonitorInput::SystemCommon(uchar status, uchar data1,
	uchar data2, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_SYSTEM_COMMON, 0, data1, data2);
	status = status;
}


void
BMidiMonitorUnit::MonitorInput::SystemRealTime(uchar status, bigtime_t time)
{
	fUnit->PushEvent(time, B_MIDI_SYSTEM_REAL_TIME, 0, status, 0);
}


BMidiMonitorUnit::BMidiMonitorUnit()
	:
	BMidiNativeUnit("MIDI Monitor", B_MEDIA_RECORDER),
	fInput(NULL),
	fWindow(NULL),
	fPaused(0),
	fStartTime(0)
{
	memset(fRing, 0, sizeof(fRing));
	fWriteIndex.store(0);
	fReadIndex.store(0);
	fDropped.store(0);

	fInput = new(std::nothrow) MonitorInput(this);
	if (fInput == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	if (RegisterInput(fInput) != B_OK) {
		delete fInput;
		fInput = NULL;
		fInitStatus = B_NO_MEMORY;
		return;
	}

	AddDiscreteParam(0, "Paused", 0, kOnOffItems, 2, B_ENABLE);
	FinishParameters("MIDI Monitor");
}


BMidiMonitorUnit::~BMidiMonitorUnit()
{
	Stop();
	if (fWindow != NULL) {
		fWindow->Lock();
		fWindow->Quit();
		fWindow = NULL;
	}
	if (fInput != NULL) {
		UnregisterInput(fInput);
		delete fInput;
	}
}


void
BMidiMonitorUnit::PushEvent(bigtime_t time, uint8 type, uchar channel,
	uchar data1, uchar data2)
{
	if (fPaused.load() != 0)
		return;

	size_t write = fWriteIndex.load(std::memory_order_relaxed);
	size_t read = fReadIndex.load(std::memory_order_acquire);
	if (write - read >= kRingCapacity) {
		fDropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	Record& record = fRing[write % kRingCapacity];
	record.time = time;
	record.type = type;
	record.channel = channel;
	record.data1 = data1;
	record.data2 = data2;
	fWriteIndex.store(write + 1, std::memory_order_release);
}


int32
BMidiMonitorUnit::ReadEvents(int32 maxCount, bigtime_t* outTimes,
	uint8* outTypes, uchar* outChannels, uchar* outData1, uchar* outData2)
{
	int32 count = 0;
	while (count < maxCount) {
		size_t read = fReadIndex.load(std::memory_order_relaxed);
		size_t write = fWriteIndex.load(std::memory_order_acquire);
		if (read == write)
			break;

		const Record& record = fRing[read % kRingCapacity];
		if (outTimes != NULL)
			outTimes[count] = record.time;
		if (outTypes != NULL)
			outTypes[count] = record.type;
		if (outChannels != NULL)
			outChannels[count] = record.channel;
		if (outData1 != NULL)
			outData1[count] = record.data1;
		if (outData2 != NULL)
			outData2[count] = record.data2;
		fReadIndex.store(read + 1, std::memory_order_release);
		count++;
	}
	return count;
}


void
BMidiMonitorUnit::SetPaused(bool paused)
{
	fPaused.store(paused ? 1 : 0);
	if (fControllable != NULL) {
		int32 value = paused ? 1 : 0;
		fControllable->SetParameterValue(0, system_time(), &value,
			sizeof(value));
	}
}


bool
BMidiMonitorUnit::IsPaused() const
{
	return fPaused.load() != 0;
}


bigtime_t
BMidiMonitorUnit::StartTime() const
{
	return fStartTime;
}


void
BMidiMonitorUnit::HandleStart(bigtime_t /*performanceTime*/)
{
	fStartTime = system_time();
	fReadIndex.store(fWriteIndex.load(std::memory_order_acquire));
	ShowWindow();
}


void
BMidiMonitorUnit::HandleStop(bigtime_t /*performanceTime*/)
{
	HideWindow();
}


void
BMidiMonitorUnit::ShowWindow()
{
	if (fWindow == NULL) {
		fWindow = new(std::nothrow) BMidiMonitorWindow(this,
			BRect(600, 150, 600 + 420, 150 + 340));
		if (fWindow == NULL)
			return;
	}
	fWindow->Show();
}


void
BMidiMonitorUnit::HideWindow()
{
	if (fWindow != NULL)
		fWindow->Hide();
}
