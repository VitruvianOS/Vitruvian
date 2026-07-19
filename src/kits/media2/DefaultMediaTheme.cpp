/*
 * Copyright 2003-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2019, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <private/media/DefaultMediaTheme.h>

#include <stdio.h>
#include <string.h>

#include <Box.h>
#include <Button.h>
#include <ChannelSlider.h>
#include <CheckBox.h>
#include <GroupView.h>
#include <MenuField.h>
#include <MessageFilter.h>
#include <OptionPopUp.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <Window.h>

#include <media2/Controllable.h>
#include <media2/ParameterWeb.h>


// ── MediaDebug stubs ────────────────────────────────────────────────────────
#define CALLED()           do {} while (0)
#define TRACE(...)         do {} while (0)
#define PRINT(...)         do {} while (0)
#define ERROR(...)         do {} while (0)


using namespace BPrivate;


namespace BPrivate {

namespace DefaultMediaControls {

class SeparatorView : public BView {
	public:
		SeparatorView(orientation orientation);
		virtual ~SeparatorView();

		virtual void Draw(BRect updateRect);

	private:
		bool	fVertical;
};

class TitleView : public BView {
	public:
		TitleView(const char *title);
		virtual ~TitleView();

		virtual void Draw(BRect updateRect);
		virtual void GetPreferredSize(float *width, float *height);

	private:
		BString fTitle;
};

class CheckBox : public BCheckBox {
	public:
		CheckBox(const char* name, const char* label,
			BDiscreteParameter &parameter);
		virtual ~CheckBox();

		virtual void AttachedToWindow();
		virtual void DetachedFromWindow();
	private:
		BDiscreteParameter &fParameter;
};

class OptionPopUp : public BOptionPopUp {
	public:
		OptionPopUp(const char* name, const char* label,
			BDiscreteParameter &parameter);
		virtual ~OptionPopUp();

		virtual void AttachedToWindow();
		virtual void DetachedFromWindow();
	private:
		BDiscreteParameter &fParameter;
};

class Slider : public BSlider {
	public:
		Slider(const char* name, const char*label, int32 minValue,
			int32 maxValue, BContinuousParameter &parameter);
		virtual ~Slider();

		virtual void AttachedToWindow();
		virtual void DetachedFromWindow();
	private:
		BContinuousParameter &fParameter;
};

class ChannelSlider : public BChannelSlider {
	public:
		ChannelSlider(const char* name, const char* label,
			orientation orientation, int32 channels,
			BContinuousParameter &parameter);
		virtual ~ChannelSlider();

		virtual void AttachedToWindow();
		virtual void DetachedFromWindow();
		virtual void UpdateToolTip(int32 currentValue);
	private:
		BContinuousParameter &fParameter;
};

class TextControl : public BTextControl {
	public:
		TextControl(const char* name, const char* label,
			BTextParameter &parameter);
		virtual ~TextControl();

		virtual void AttachedToWindow();
		virtual void DetachedFromWindow();
	private:
		BTextParameter &fParameter;
};

class MessageFilter : public BMessageFilter {
	public:
		static MessageFilter *FilterFor(BView *view, BParameter &parameter);

	protected:
		MessageFilter();
};

class ContinuousMessageFilter : public MessageFilter {
	public:
		ContinuousMessageFilter(BControl *control,
			BContinuousParameter &parameter);
		virtual ~ContinuousMessageFilter();

		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		void _UpdateControl();

		BControl				*fControl;
		BContinuousParameter	&fParameter;
};

class DiscreteMessageFilter : public MessageFilter {
	public:
		DiscreteMessageFilter(BControl *control, BDiscreteParameter &parameter);
		virtual ~DiscreteMessageFilter();

		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		BDiscreteParameter	&fParameter;
};

class TextMessageFilter : public MessageFilter {
	public:
		TextMessageFilter(BControl *control, BTextParameter &parameter);
		virtual ~TextMessageFilter();

		virtual filter_result Filter(BMessage *message, BHandler **target);

	private:
		BTextParameter	&fParameter;
};

};

using namespace DefaultMediaControls;

}	// namespace BPrivate


const uint32 kMsgParameterChanged = '_mPC';


static bool
parameter_should_be_hidden(BParameter &parameter)
{
	if (parameter.Type() != BParameter::B_NULL_PARAMETER
		|| strcmp(parameter.Kind(), B_WEB_PHYSICAL_INPUT))
		return false;

	for (int32 i = 0; i < parameter.CountOutputs(); i++) {
		if (!strcmp(parameter.OutputAt(0)->Kind(), B_INPUT_MUX))
			return true;
	}

	return false;
}


static void
start_watching_for_parameter_changes(BControl* /*control*/, BParameter& /*parameter*/)
{
	// media2 has no BMediaRoster — parameter change broadcasts are delivered
	// directly via BControllable::BroadcastChangedParameter to messengers
	// registered through StartControlPanel. UI controls are not auto-watched
	// in this minimal port.
}


static void
stop_watching_for_parameter_changes(BControl* /*control*/, BParameter& /*parameter*/)
{
}


//	#pragma mark -


SeparatorView::SeparatorView(orientation orientation)
	: BView("-", B_WILL_DRAW),
	fVertical(orientation == B_VERTICAL)
{
	if (fVertical) {
		SetExplicitMinSize(BSize(5, 0));
		SetExplicitMaxSize(BSize(5, MaxSize().height));
	} else {
		SetExplicitMinSize(BSize(0, 5));
		SetExplicitMaxSize(BSize(MaxSize().width, 5));
	}
}


SeparatorView::~SeparatorView()
{
}


void
SeparatorView::Draw(BRect updateRect)
{
	rgb_color color = ui_color(B_PANEL_BACKGROUND_COLOR);
	BRect rect = updateRect & Bounds();

	SetHighColor(tint_color(color, B_DARKEN_1_TINT));
	if (fVertical)
		StrokeLine(BPoint(0, rect.top), BPoint(0, rect.bottom));
	else
		StrokeLine(BPoint(rect.left, 0), BPoint(rect.right, 0));

	SetHighColor(tint_color(color, B_LIGHTEN_1_TINT));
	if (fVertical)
		StrokeLine(BPoint(1, rect.top), BPoint(1, rect.bottom));
	else
		StrokeLine(BPoint(rect.left, 1), BPoint(rect.right, 1));
}


//	#pragma mark -


TitleView::TitleView(const char *title)
	: BView(title, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fTitle(title)
{
	AdoptSystemColors();
}


TitleView::~TitleView()
{
}


void
TitleView::Draw(BRect updateRect)
{
	BRect rect(Bounds());
	rect.left = (rect.Width() - StringWidth(fTitle)) / 2;

	SetDrawingMode(B_OP_OVER);
	SetHighColor(mix_color(ui_color(B_PANEL_TEXT_COLOR), make_color(255, 0, 0), 100));
	DrawString(fTitle, BPoint(rect.left, rect.bottom - 9));
}


void
TitleView::GetPreferredSize(float *_width, float *_height)
{
	if (_width)
		*_width = StringWidth(fTitle) + 2;

	if (_height) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		*_height = fontHeight.ascent + fontHeight.descent + fontHeight.leading + 8;
	}
}


//	#pragma mark -


CheckBox::CheckBox(const char* name, const char* label,
	BDiscreteParameter &parameter)
	: BCheckBox(name, label, NULL),
	fParameter(parameter)
{
}


CheckBox::~CheckBox()
{
}


void
CheckBox::AttachedToWindow()
{
	BCheckBox::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


void
CheckBox::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


OptionPopUp::OptionPopUp(const char* name, const char* label,
	BDiscreteParameter &parameter)
	: BOptionPopUp(name, label, NULL),
	fParameter(parameter)
{
}


OptionPopUp::~OptionPopUp()
{
}


void
OptionPopUp::AttachedToWindow()
{
	BOptionPopUp::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


void
OptionPopUp::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


Slider::Slider(const char* name, const char* label, int32 minValue,
	int32 maxValue, BContinuousParameter &parameter)
	: BSlider(name, label, NULL, minValue, maxValue, B_HORIZONTAL),
	fParameter(parameter)
{
}


Slider::~Slider()
{
}


void
Slider::AttachedToWindow()
{
	BSlider::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


void
Slider::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


ChannelSlider::ChannelSlider(const char* name, const char* label,
	orientation orientation, int32 channels, BContinuousParameter &parameter)
	: BChannelSlider(name, label, NULL, orientation, channels),
	fParameter(parameter)
{
}


ChannelSlider::~ChannelSlider()
{
}


void
ChannelSlider::AttachedToWindow()
{
	BChannelSlider::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


void
ChannelSlider::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);

	BChannelSlider::DetachedFromWindow();
}


void
ChannelSlider::UpdateToolTip(int32 currentValue)
{
	BString valueString;
	valueString.SetToFormat("%.1f", currentValue / 1000.0);
	SetToolTip(valueString);
}


TextControl::TextControl(const char* name, const char* label,
	BTextParameter &parameter)
	: BTextControl(name, label, "", NULL),
	fParameter(parameter)
{
}


TextControl::~TextControl()
{
}


void
TextControl::AttachedToWindow()
{
	BTextControl::AttachedToWindow();

	SetTarget(this);
	start_watching_for_parameter_changes(this, fParameter);
}


void
TextControl::DetachedFromWindow()
{
	stop_watching_for_parameter_changes(this, fParameter);
}


//	#pragma mark -


MessageFilter::MessageFilter()
	: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE)
{
}


MessageFilter *
MessageFilter::FilterFor(BView *view, BParameter &parameter)
{
	BControl *control = dynamic_cast<BControl *>(view);
	if (control == NULL)
		return NULL;

	switch (parameter.Type()) {
		case BParameter::B_CONTINUOUS_PARAMETER:
			return new ContinuousMessageFilter(control,
				static_cast<BContinuousParameter &>(parameter));

		case BParameter::B_DISCRETE_PARAMETER:
			return new DiscreteMessageFilter(control,
				static_cast<BDiscreteParameter &>(parameter));

		case BParameter::B_TEXT_PARAMETER:
			return new TextMessageFilter(control,
				static_cast<BTextParameter &>(parameter));

		case BParameter::B_NULL_PARAMETER: /* fall through */
		default:
			return NULL;
	}
}


//	#pragma mark -


ContinuousMessageFilter::ContinuousMessageFilter(BControl *control,
		BContinuousParameter &parameter)
	: MessageFilter(),
	fControl(control),
	fParameter(parameter)
{
	control->SetMessage(new BMessage(kMsgParameterChanged));

	if (BSlider *slider = dynamic_cast<BSlider *>(fControl))
		slider->SetModificationMessage(new BMessage(kMsgParameterChanged));
	else if (BChannelSlider *slider = dynamic_cast<BChannelSlider *>(fControl))
		slider->SetModificationMessage(new BMessage(kMsgParameterChanged));

	_UpdateControl();
}


ContinuousMessageFilter::~ContinuousMessageFilter()
{
}


filter_result
ContinuousMessageFilter::Filter(BMessage *message, BHandler **target)
{
	if (*target != fControl)
		return B_DISPATCH_MESSAGE;

	if (message->what == kMsgParameterChanged) {
		float value[fParameter.CountChannels()];

		if (BSlider *slider = dynamic_cast<BSlider *>(fControl)) {
			value[0] = (float)(slider->Value() / 1000.0);
		} else if (BChannelSlider *slider
				= dynamic_cast<BChannelSlider *>(fControl)) {
			for (int32 i = 0; i < fParameter.CountChannels(); i++)
				value[i] = (float)(slider->ValueFor(i) / 1000.0);
		}

		if (fParameter.SetValue((void *)value, sizeof(value), -1) < B_OK)
			return B_DISPATCH_MESSAGE;
		return B_SKIP_MESSAGE;
	}
	if (message->what == B_MEDIA_NEW_PARAMETER_VALUE) {
		int32 parameterID;
		if (message->FindInt32("be:parameter", &parameterID) != B_OK
			|| fParameter.ID() != parameterID)
			return B_DISPATCH_MESSAGE;

		_UpdateControl();
		return B_SKIP_MESSAGE;
	}

	return B_DISPATCH_MESSAGE;
}


void
ContinuousMessageFilter::_UpdateControl()
{
	float value[fParameter.CountChannels()];
	size_t size = sizeof(value);
	if (fParameter.GetValue((void *)&value, &size, NULL) < B_OK)
		return;

	if (BSlider *slider = dynamic_cast<BSlider *>(fControl)) {
		slider->SetValue((int32) (1000 * value[0]));
		slider->SetModificationMessage(new BMessage(kMsgParameterChanged));
	} else if (BChannelSlider *slider
			= dynamic_cast<BChannelSlider *>(fControl)) {
		for (int32 i = 0; i < fParameter.CountChannels(); i++) {
			slider->SetValueFor(i, (int32) (1000 * value[i]));
		}
	}
}


//	#pragma mark -


DiscreteMessageFilter::DiscreteMessageFilter(BControl *control,
		BDiscreteParameter &parameter)
	: MessageFilter(),
	fParameter(parameter)
{
	control->SetMessage(new BMessage(kMsgParameterChanged));

	size_t size = sizeof(int32);
	int32 value;
	if (parameter.GetValue((void *)&value, &size, NULL) < B_OK)
		return;

	if (BCheckBox *checkBox = dynamic_cast<BCheckBox *>(control)) {
		checkBox->SetValue(value);
	} else if (BOptionPopUp *popUp = dynamic_cast<BOptionPopUp *>(control)) {
		popUp->SelectOptionFor(value);
	}
}


DiscreteMessageFilter::~DiscreteMessageFilter()
{
}


filter_result
DiscreteMessageFilter::Filter(BMessage *message, BHandler **target)
{
	BControl *control;

	if ((control = dynamic_cast<BControl *>(*target)) == NULL)
		return B_DISPATCH_MESSAGE;

	if (message->what == B_MEDIA_NEW_PARAMETER_VALUE) {
		int32 parameterID;
		if (message->FindInt32("be:parameter", &parameterID) != B_OK
			|| fParameter.ID() != parameterID)
			return B_DISPATCH_MESSAGE;

		int32 value = 0;
		size_t valueSize = sizeof(int32);
		if (fParameter.GetValue((void*)&value, &valueSize, NULL) < B_OK)
			return B_SKIP_MESSAGE;
		if (BCheckBox* checkBox = dynamic_cast<BCheckBox*>(control)) {
			checkBox->SetValue(value);
		} else if (BOptionPopUp* popUp = dynamic_cast<BOptionPopUp*>(control)) {
			popUp->SetValue(value);
		}

		return B_SKIP_MESSAGE;
	}

	if (message->what != kMsgParameterChanged)
		return B_DISPATCH_MESSAGE;

	int32 value = 0;

	if (BCheckBox *checkBox = dynamic_cast<BCheckBox *>(control)) {
		value = checkBox->Value();
	} else if (BOptionPopUp *popUp = dynamic_cast<BOptionPopUp *>(control)) {
		popUp->SelectedOption(NULL, &value);
	}

	if (fParameter.SetValue((void *)&value, sizeof(value), -1) < B_OK)
		return B_DISPATCH_MESSAGE;

	return B_SKIP_MESSAGE;
}


//	#pragma mark -


TextMessageFilter::TextMessageFilter(BControl *control,
		BTextParameter &parameter)
	: MessageFilter(),
	fParameter(parameter)
{
	control->SetMessage(new BMessage(kMsgParameterChanged));

	if (BTextControl *textControl = dynamic_cast<BTextControl *>(control)) {
		size_t valueSize = parameter.MaxBytes();
		char* value = new char[valueSize + 1];

		if (parameter.GetValue((void *)value, &valueSize, NULL) >= B_OK)
			textControl->SetText(value);

		delete[] value;
	}
}


TextMessageFilter::~TextMessageFilter()
{
}


filter_result
TextMessageFilter::Filter(BMessage *message, BHandler **target)
{
	BControl *control;

	if ((control = dynamic_cast<BControl *>(*target)) == NULL)
		return B_DISPATCH_MESSAGE;

	if (message->what == B_MEDIA_NEW_PARAMETER_VALUE) {
		int32 parameterID;
		if (message->FindInt32("be:parameter", &parameterID) != B_OK
			|| fParameter.ID() != parameterID)
			return B_DISPATCH_MESSAGE;

		if (BTextControl *textControl = dynamic_cast<BTextControl *>(control)) {
			size_t valueSize = fParameter.MaxBytes();
			char* value = new char[valueSize + 1];
			if (fParameter.GetValue((void *)value, &valueSize, NULL) >= B_OK)
				textControl->SetText(value);

			delete[] value;

			return B_SKIP_MESSAGE;
		}

		return B_DISPATCH_MESSAGE;
	}

	if (message->what != kMsgParameterChanged)
		return B_DISPATCH_MESSAGE;

	if (BTextControl *textControl = dynamic_cast<BTextControl *>(control)) {
		BString value = textControl->Text();
		if (fParameter.SetValue((void *)value.String(), value.Length() + 1, -1) < B_OK)
			return B_DISPATCH_MESSAGE;
	}

	return B_SKIP_MESSAGE;
}


//	#pragma mark -


DefaultMediaTheme::DefaultMediaTheme()
	: BMediaTheme("Haiku theme", "Haiku built-in theme version 0.1")
{
	CALLED();
}


BControl *
DefaultMediaTheme::MakeControlFor(BParameter *parameter)
{
	CALLED();

	return MakeViewFor(parameter);
}


BView *
DefaultMediaTheme::MakeViewFor(BParameterWeb *web, const BRect *hintRect)
{
	CALLED();

	if (web == NULL)
		return NULL;

	BTabView *tabView = NULL;
	if (web->CountGroups() > 1)
		tabView = new BTabView("web");

	for (int32 i = 0; i < web->CountGroups(); i++) {
		BParameterGroup *group = web->GroupAt(i);
		if (group == NULL)
			continue;

		BView *groupView = MakeViewFor(*group);
		if (groupView == NULL)
			continue;

		BScrollView *scrollView = new BScrollView(groupView->Name(), groupView, 0,
			true, true, B_NO_BORDER);
		scrollView->SetExplicitMinSize(BSize(B_V_SCROLL_BAR_WIDTH,
			B_H_SCROLL_BAR_HEIGHT));
		if (tabView == NULL) {
			if (hintRect != NULL) {
				scrollView->MoveTo(hintRect->LeftTop());
				scrollView->ResizeTo(hintRect->Size());
			} else {
				scrollView->ResizeTo(600, 400);
			}
			return scrollView;
		}
		tabView->AddTab(scrollView);
	}

	if (hintRect != NULL) {
		tabView->MoveTo(hintRect->LeftTop());
		tabView->ResizeTo(hintRect->Size());
	} else {
		tabView->ResizeTo(600, 400);
	}
	return tabView;
}


BView *
DefaultMediaTheme::MakeViewFor(BParameterGroup& group)
{
	CALLED();

	if (group.Flags() & B_HIDDEN_PARAMETER)
		return NULL;

	BGroupView *view = new BGroupView(group.Name(), B_HORIZONTAL,
		B_USE_HALF_ITEM_SPACING);
	BGroupLayout *layout = view->GroupLayout();
	layout->SetInsets(B_USE_HALF_ITEM_INSETS);

	if (group.CountParameters() > 0) {
		BGroupView *paramView = new BGroupView(group.Name(), B_VERTICAL,
			B_USE_HALF_ITEM_SPACING);
		BGroupLayout *paramLayout = paramView->GroupLayout();
		paramLayout->SetInsets(0);

		for (int32 i = 0; i < group.CountParameters(); i++) {
			BParameter *parameter = group.ParameterAt(i);
			if (parameter == NULL)
				continue;

			BView *parameterView = MakeSelfHostingViewFor(*parameter);
			if (parameterView == NULL)
				continue;

			paramLayout->AddView(parameterView);
		}
		paramLayout->AddItem(BSpaceLayoutItem::CreateHorizontalStrut(10));
		layout->AddView(paramView);
	}

	for (int32 i = 0; i < group.CountGroups(); i++) {
		BParameterGroup *subGroup = group.GroupAt(i);
		if (subGroup == NULL)
			continue;

		BView *groupView = MakeViewFor(*subGroup);
		if (groupView == NULL)
			continue;

		if (i > 0)
			layout->AddView(new SeparatorView(B_VERTICAL));

		layout->AddView(groupView);
	}

	layout->AddItem(BSpaceLayoutItem::CreateGlue());
	return view;
}


BView *
DefaultMediaTheme::MakeSelfHostingViewFor(BParameter& parameter)
{
	if (parameter.Flags() & B_HIDDEN_PARAMETER
		|| parameter_should_be_hidden(parameter))
		return NULL;

	BView *view = MakeViewFor(&parameter);
	if (view == NULL) {
		if (parameter.Type() == BParameter::B_NULL_PARAMETER) {
			if (parameter.Group()->ParameterAt(0) == &parameter) {
				return new TitleView(parameter.Name());
			}
			BStringView *stringView = new BStringView(parameter.Name(),
				parameter.Name());
			stringView->SetAlignment(B_ALIGN_CENTER);

			return stringView;
		}

		return NULL;
	}

	MessageFilter *filter = MessageFilter::FilterFor(view, parameter);
	if (filter != NULL)
		view->AddFilter(filter);

	return view;
}


BControl *
DefaultMediaTheme::MakeViewFor(BParameter *parameter)
{
	switch (parameter->Type()) {
		case BParameter::B_NULL_PARAMETER:
			return NULL;

		case BParameter::B_DISCRETE_PARAMETER:
		{
			BDiscreteParameter &discrete
				= static_cast<BDiscreteParameter &>(*parameter);

			if (!strcmp(discrete.Kind(), B_ENABLE)
				|| !strcmp(discrete.Kind(), B_MUTE)
				|| discrete.CountItems() == 0) {
				return new CheckBox(discrete.Name(), discrete.Name(), discrete);
			} else {
				BOptionPopUp *popUp = new OptionPopUp(discrete.Name(),
					discrete.Name(), discrete);

				for (int32 i = 0; i < discrete.CountItems(); i++) {
					popUp->AddOption(discrete.ItemNameAt(i),
						discrete.ItemValueAt(i));
				}

				return popUp;
			}
		}

		case BParameter::B_CONTINUOUS_PARAMETER:
		{
			BContinuousParameter &continuous
				= static_cast<BContinuousParameter &>(*parameter);

			if (!strcmp(continuous.Kind(), B_MASTER_GAIN)
				|| !strcmp(continuous.Kind(), B_GAIN)) {
				BChannelSlider *slider = new ChannelSlider(
					continuous.Name(), continuous.Name(), B_VERTICAL,
					continuous.CountChannels(), continuous);

				BString minLabel, maxLabel;
				const char *unit = continuous.Unit();
				if (unit[0]) {
					minLabel.SetToFormat("%g %s", continuous.MinValue(), continuous.Unit());
					maxLabel.SetToFormat("%g %s", continuous.MaxValue(), continuous.Unit());
				} else {
					minLabel.SetToFormat("%g", continuous.MinValue());
					maxLabel.SetToFormat("%g", continuous.MaxValue());
				}
				slider->SetLimitLabels(minLabel, maxLabel);

				for (int32 i = 0; i < continuous.CountChannels(); i++) {
					slider->SetLimitsFor(i, int32(continuous.MinValue() * 1000),
						int32(continuous.MaxValue() * 1000));
				}

				return slider;
			}

			BSlider *slider = new Slider(parameter->Name(),
				parameter->Name(), int32(continuous.MinValue() * 1000),
				int32(continuous.MaxValue() * 1000), continuous);

			return slider;
		}

		case BParameter::B_TEXT_PARAMETER:
		{
			BTextParameter &text
				= static_cast<BTextParameter &>(*parameter);
			return new TextControl(text.Name(), text.Name(), text);
		}

		default:
			ERROR("BMediaTheme: Don't know parameter type: 0x%x\n",
				parameter->Type());
	}
	return NULL;
}
