/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "InterfaceDefs.h"
#include <Application.h>
#include <ControlLook.h>
#include <GridLayout.h>
#include <GroupLayoutBuilder.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <Window.h>

#include <stdio.h>

// Slider with update text but no label has a broken triangle thumb.
class UpdateTextSlider : public BSlider {
	public:
		UpdateTextSlider(const char* name, thumb_style thumbType)
			: BSlider(name, NULL, NULL, 0, 100, B_HORIZONTAL, thumbType)
		{
			SetValue(50);
		}

		virtual const char* UpdateText() const
		{
			snprintf(fText, sizeof(fText), "%" B_PRId32, Value());
			return fText;
		}

	private:
		mutable char fText[16];
};


class Window : public BWindow {
	public:
		Window();

		virtual bool QuitRequested();
};


//	#pragma mark -


Window::Window()
	: BWindow(BRect(100, 100, 800, 500),
	      "Slider-Test", B_DOCUMENT_WINDOW,
	      B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
		| B_QUIT_ON_WINDOW_CLOSE)
{

	
	rgb_color fillColor = { 200, 80, 80, 255 };

	// horizontal sliders
	BSlider* slider1 = new BSlider("Slider1", "Test 1", NULL, 0, 100, B_HORIZONTAL);
	BSlider* slider2 = new BSlider("Slider2", "Test 2", NULL, 0, 100, B_HORIZONTAL, B_TRIANGLE_THUMB);

	BSlider* slider3 = new BSlider("Slider3", "Test 3", NULL, 0, 100, B_HORIZONTAL);
	slider3->UseFillColor(true, &fillColor);
	slider3->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider3->SetHashMarkCount(11);
	slider3->SetLimitLabels("0", "100");
	slider3->SetBarThickness(12.0);

	BSlider* slider4 = new BSlider("Slider4", "Test 4", NULL, 0, 100, B_HORIZONTAL, B_TRIANGLE_THUMB);
	slider4->SetLimitLabels("0", "100");
	slider4->UseFillColor(true, &fillColor);
	slider4->SetBarThickness(20.0);
	slider4->SetHashMarks(B_HASH_MARKS_BOTH);
	slider4->SetHashMarkCount(21);

	BSlider* slider9 = new UpdateTextSlider("Slider9", B_TRIANGLE_THUMB);
	BSlider* slider10 = new UpdateTextSlider("Slider10", B_BLOCK_THUMB);

	BSlider *sliderA = new BSlider("SliderA", "Test A", NULL, 0, 100, B_HORIZONTAL);
	sliderA->SetLimitLabels("0", "100");
	sliderA->UseFillColor(true, &fillColor);
	sliderA->SetHashMarks(B_HASH_MARKS_BOTH);
	sliderA->SetHashMarkCount(5);
	sliderA->SetExplicitMaxSize(sliderA->MinSize());

	// vertical sliders
	BSlider* slider5 = new BSlider("Slider5", "Test 5", NULL, 0, 100, B_VERTICAL);
	slider5->SetBarThickness(12.0);
	slider5->SetHashMarks(B_HASH_MARKS_RIGHT);
	slider5->SetHashMarkCount(5);

	BSlider* slider6 = new BSlider("Slider6", "Test 6", NULL, 0, 100, B_VERTICAL, B_TRIANGLE_THUMB);

	BSlider* slider7 = new BSlider("Slider7", "Test 7", NULL, 0, 100, B_VERTICAL);
	slider7->UseFillColor(true, &fillColor);
	slider7->SetHashMarks(B_HASH_MARKS_BOTH);
	slider7->SetBarThickness(6.0);
	slider7->SetHashMarkCount(11);
	slider7->SetLimitLabels("0", "100");

	BSlider* slider8 = new BSlider("Slider8", "Test 8", NULL, 0, 100, B_VERTICAL, B_TRIANGLE_THUMB);
	slider8->SetOrientation(B_VERTICAL);
	slider8->UseFillColor(true, &fillColor);
	slider8->SetBarThickness(20.0);
	slider8->SetHashMarks(B_HASH_MARKS_BOTH);
	slider8->SetHashMarkCount(21);
	slider8->SetLimitLabels("0", "100");
	
	BSlider *sliderB = new BSlider("SliderB", "Test B", NULL, 0, 100, B_VERTICAL);
	sliderB->SetLimitLabels("0", "100");
	sliderB->UseFillColor(true, &fillColor);
	sliderB->SetHashMarks(B_HASH_MARKS_BOTH);
	sliderB->SetHashMarkCount(5);
	sliderB->SetExplicitMaxSize(sliderB->MinSize());

	BGroupLayout* h_group =
		BLayoutBuilder::Group<>(B_VERTICAL)
			.Add(slider1)
			.Add(slider2)
			.Add(slider3)
			.Add(slider4)
			.Add(slider9)
			.Add(slider10)
			.AddGroup(B_HORIZONTAL)
				.Add(sliderA)
				.AddGlue()
			.End()
			.AddGlue();
	BGroupLayout* v_group =
		BLayoutBuilder::Group<>(B_HORIZONTAL)
			.Add(slider5)
			.Add(slider6)
			.Add(slider7)
			.Add(slider8)
			.AddGroup(B_VERTICAL)
				.Add(sliderB)
				.AddGlue()
			.End()
			.AddGlue();
	h_group->SetExplicitMinSize(BSize(400, B_SIZE_UNSET));


	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(BControlLook::ComposeSpacing(B_USE_WINDOW_SPACING))
		.Add(h_group)
		.Add(v_group);
}


bool
Window::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


//	#pragma mark -


class Application : public BApplication {
	public:
		Application();

		virtual void ReadyToRun(void);
};


Application::Application()
	: BApplication("application/x-vnd.haiku-test")
{
}


void
Application::ReadyToRun(void)
{
	BWindow *window = new Window();
	window->Show();
}


//	#pragma mark -


int 
main(int argc, char **argv)
{
	Application app;

	app.Run();
	return 0;
}

