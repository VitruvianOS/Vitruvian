/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaTheme.h>

#include <stdio.h>

#include <Box.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StringView.h>
#include <SpaceLayoutItem.h>

#include <media2/ParameterWeb.h>

#include <private/media/DefaultMediaTheme.h>


namespace {

#if 0	// Replaced by BPrivate::DefaultMediaTheme imported from haiku-latest.
class DefaultTheme : public BMediaTheme {
public:
	DefaultTheme()
		:
		BMediaTheme("Default", "Default Vitruvian theme", NULL, 0)
	{}

	virtual BControl* MakeControlFor(BParameter* parameter) override
	{
		// MakeControlFor must return a BControl; only BContinuousParameter
		// fits (BSlider IS-A BControl). Discrete parameters become a
		// BMenuField (BView, not BControl) — handled inline in MakeViewFor.
		if (parameter == NULL
				|| parameter->Type() != BParameter::B_CONTINUOUS_PARAMETER) {
			return NULL;
		}
		BContinuousParameter* cp = (BContinuousParameter*)parameter;
		BSlider* s = new BSlider(parameter->Name(), parameter->Name(),
			NULL, (int32)(cp->MinValue() * 100),
			(int32)(cp->MaxValue() * 100), B_HORIZONTAL);
		s->SetHashMarks(B_HASH_MARKS_BOTTOM);
		s->SetHashMarkCount(5);
		return s;
	}

private:
	static BView* _MakeDiscreteField(BDiscreteParameter* dp)
	{
		BPopUpMenu* m = new BPopUpMenu(dp->Name());
		for (int32 i = 0; i < dp->CountItems(); i++)
			m->AddItem(new BMenuItem(dp->ItemNameAt(i), NULL));
		return new BMenuField(dp->Name(), dp->Name(), m);
	}

protected:
	virtual BView* MakeViewFor(BParameterWeb* web,
		const BRect* /*hintRect*/) override
	{
		if (web == NULL)
			return NULL;
		BGroupView* root = new BGroupView(B_VERTICAL, 4);
		BGroupLayout* layout = root->GroupLayout();

		for (int32 g = 0; g < web->CountGroups(); g++) {
			BParameterGroup* group = web->GroupAt(g);
			BBox* box = new BBox(group->Name());
			box->SetLabel(group->Name());
			BGroupView* inner = new BGroupView(B_VERTICAL, 2);

			for (int32 p = 0; p < group->CountParameters(); p++) {
				BParameter* param = group->ParameterAt(p);
				if (param->Type() == BParameter::B_NULL_PARAMETER) {
					inner->GroupLayout()->AddView(
						new BStringView(param->Name(), param->Name()));
				} else if (param->Type() == BParameter::B_DISCRETE_PARAMETER) {
					inner->GroupLayout()->AddView(
						_MakeDiscreteField((BDiscreteParameter*)param));
				} else {
					BControl* ctrl = MakeControlFor(param);
					if (ctrl != NULL)
						inner->GroupLayout()->AddView(ctrl);
				}
			}
			inner->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
			box->AddChild(inner);
			layout->AddView(box);
		}
		layout->AddItem(BSpaceLayoutItem::CreateGlue());
		return root;
	}
};
#endif	// minimal DefaultTheme replaced.


static BMediaTheme*	sPreferredTheme = NULL;


static BMediaTheme*
GetOrInitTheme()
{
	if (sPreferredTheme == NULL)
		sPreferredTheme = new BPrivate::DefaultMediaTheme();
	return sPreferredTheme;
}

} // anonymous namespace


// #pragma mark - BMediaTheme


BMediaTheme::BMediaTheme(const char* /*name*/, const char* /*info*/,
	const entry_ref* /*addOn*/, int32 /*themeID*/)
{
}


BMediaTheme::~BMediaTheme()
{
}


const char*  BMediaTheme::Name()                { return "Default"; }
const char*  BMediaTheme::Info()                { return "Default Vitruvian theme"; }
int32        BMediaTheme::ID()                  { return 0; }
bool         BMediaTheme::GetRef(entry_ref*)    { return false; }


BView*
BMediaTheme::ViewFor(BParameterWeb* web, const BRect* hintRect,
	BMediaTheme* usingTheme)
{
	BMediaTheme* theme = usingTheme != NULL ? usingTheme : GetOrInitTheme();
	if (theme == NULL || web == NULL)
		return NULL;
	return theme->MakeViewFor(web, hintRect);
}


status_t
BMediaTheme::SetPreferredTheme(BMediaTheme* defaultTheme)
{
	if (sPreferredTheme != NULL && sPreferredTheme != defaultTheme)
		delete sPreferredTheme;
	sPreferredTheme = defaultTheme;
	return B_OK;
}


BMediaTheme*
BMediaTheme::PreferredTheme()
{
	return GetOrInitTheme();
}


BBitmap*
BMediaTheme::BackgroundBitmapFor(bg_kind)
{
	return NULL;
}


rgb_color
BMediaTheme::BackgroundColorFor(bg_kind)
{
	const rgb_color c = { 216, 216, 216, 255 };
	return c;
}


rgb_color
BMediaTheme::ForegroundColorFor(fg_kind)
{
	const rgb_color c = { 0, 0, 0, 255 };
	return c;
}


BControl*
BMediaTheme::MakeFallbackViewFor(BParameter* parameter)
{
	BMediaTheme* t = GetOrInitTheme();
	return t != NULL ? t->MakeControlFor(parameter) : NULL;
}


// FBC reserved
status_t BMediaTheme::_Reserved_ControlTheme_0(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_1(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_2(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_3(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_4(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_5(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_6(void*) { return B_ERROR; }
status_t BMediaTheme::_Reserved_ControlTheme_7(void*) { return B_ERROR; }
