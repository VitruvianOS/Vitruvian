#include "GraphicsDefs.h"
#include <Application.h>
#include <Box.h>
#include <ControlLook.h>
#include <Font.h>
#include <GridView.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>


// Checks BControlLook::GetArrowShape against the bespoke triangle math it
// replaces.

struct ArrowCase {
	const char*	name;
	BRect		source;
	uint32		direction;
	void		(*Original)(BRect source, BPoint points[3]);
	BRect		(*ShapeRect)(BRect source);
};


// HaikuControlLook.cpp _DrawPopUpMarker

static void
haiku_marker_original(BRect rect, BPoint points[3])
{
	BPoint center(roundf((rect.left + rect.right) / 2.0),
		roundf((rect.top + rect.bottom) / 2.0));
	const float metric = roundf(rect.Width() * 3.125f) / 10.0f,
		offset = ceilf((metric * 0.2f) * 10.0f) / 10.0f;
	points[0] = center + BPoint(-metric, -offset);
	points[1] = center + BPoint(metric, -offset);
	points[2] = center + BPoint(0.0, metric * 0.8f);
}


static BRect
haiku_marker_rect(BRect rect)
{
	BPoint center(roundf((rect.left + rect.right) / 2.0),
		roundf((rect.top + rect.bottom) / 2.0));
	const float metric = roundf(rect.Width() * 3.125f) / 10.0f;
	return BRect(center.x - metric, center.y - metric / 2,
		center.x + metric, center.y + metric / 2);
}


// BeControlLook.cpp _DrawPopUpMarker

static void
be_marker_original(BRect rect, BPoint points[3])
{
	BPoint position(rect.right - 8, rect.bottom - 8);
	points[0] = position + BPoint(-2.5, -0.5);
	points[1] = position + BPoint(2.5, -0.5);
	points[2] = position + BPoint(0.0, 2.0);
}


static BRect
be_marker_rect(BRect rect)
{
	BPoint position(rect.right - 8, rect.bottom - 8);
	return BRect(position.x - 2.5, position.y - 1.25, position.x + 2.5,
		position.y + 1.25);
}


// FlatControlLook.cpp _DrawPopUpMarker

static void
flat_marker_original(BRect rect, BPoint points[3])
{
	BPoint center(roundf((rect.left + rect.right) / 2.0),
		roundf((rect.top + rect.bottom) / 2.0));
	points[0] = center + BPoint(-2.5, -0.5);
	points[1] = center + BPoint(2.5, -0.5);
	points[2] = center + BPoint(0.0, 2.0);
}


static BRect
flat_marker_rect(BRect rect)
{
	BPoint center(roundf((rect.left + rect.right) / 2.0),
		roundf((rect.top + rect.bottom) / 2.0));
	return BRect(center.x - 2.5, center.y - 1.25, center.x + 2.5,
		center.y + 1.25);
}


// tracker/TitleView.cpp BTitleView::Draw. 
static BPoint
tracker_center(BRect bounds)
{
	return BPoint(bounds.left + 20 - 6,
		roundf((bounds.top + bounds.bottom) / 2.0));
}


static void
tracker_down_original(BRect bounds, BPoint points[3])
{
	BPoint center = tracker_center(bounds);
	points[0] = center + BPoint(-3.5, -1.5);
	points[1] = center + BPoint(3.5, -1.5);
	points[2] = center + BPoint(0.0, 2.0);
}


static BRect
tracker_down_rect(BRect bounds)
{
	BPoint center = tracker_center(bounds);
	return BRect(center.x - 3.5, center.y - 1.75, center.x + 3.5,
		center.y + 1.75);
}


static void
tracker_up_original(BRect bounds, BPoint points[3])
{
	BPoint center = tracker_center(bounds);
	points[0] = center + BPoint(-3.5, 1.5);
	points[1] = center + BPoint(3.5, 1.5);
	points[2] = center + BPoint(0.0, -2.0);
}


static BRect
tracker_up_rect(BRect bounds)
{
	BPoint center = tracker_center(bounds);
	return BRect(center.x - 3.5, center.y - 1.75, center.x + 3.5,
		center.y + 1.75);
}


// MenuWindow.cpp BMenuScroller, and the same code in deskbar
// InlineScrollView.cpp. Both use a scroller dimension of 12.

static const int kScroller = 12;


static void
scroller_up_original(BRect bounds, BPoint points[3])
{
	float middle = bounds.right / 2;
	points[0].Set(middle, (kScroller / 2) - 3);
	points[1].Set(middle + 5, (kScroller / 2) + 2);
	points[2].Set(middle - 5, (kScroller / 2) + 2);
}


static BRect
scroller_up_rect(BRect bounds)
{
	const float kArrowSpan = 10;
	const float kArrowDepth = 5;
	const float middle = bounds.right / 2;
	const float center = kScroller / 2;
	return BRect(middle - kArrowSpan / 2, center - kArrowDepth / 2,
		middle + kArrowSpan / 2, center + kArrowDepth / 2);
}


static void
scroller_down_original(BRect frame, BPoint points[3])
{
	float middle = frame.right / 2;
	points[0].Set(middle, frame.bottom - (kScroller / 2) + 3);
	points[1].Set(middle + 5, frame.bottom - (kScroller / 2) - 2);
	points[2].Set(middle - 5, frame.bottom - (kScroller / 2) - 2);
}


static BRect
scroller_down_rect(BRect frame)
{
	const float kArrowSpan = 10;
	const float kArrowDepth = 5;
	const float middle = frame.right / 2;
	const float center = frame.bottom - kScroller / 2;
	return BRect(middle - kArrowSpan / 2, center - kArrowDepth / 2,
		middle + kArrowSpan / 2, center + kArrowDepth / 2);
}


static void
scroller_left_original(BRect bounds, BPoint points[3])
{
	float middle = bounds.bottom / 2;
	points[0].Set((kScroller / 2) - 3, middle);
	points[1].Set((kScroller / 2) + 2, middle + 5);
	points[2].Set((kScroller / 2) + 2, middle - 5);
}


static BRect
scroller_left_rect(BRect bounds)
{
	const float kArrowSpan = 10;
	const float kArrowDepth = 5;
	const float middle = bounds.bottom / 2;
	const float center = kScroller / 2;
	return BRect(center - kArrowDepth / 2, middle - kArrowSpan / 2,
		center + kArrowDepth / 2, middle + kArrowSpan / 2);
}


static void
scroller_right_original(BRect bounds, BPoint points[3])
{
	float middle = bounds.bottom / 2;
	points[0].Set(kScroller / 2 + 3, middle);
	points[1].Set(kScroller / 2 - 2, middle + 5);
	points[2].Set(kScroller / 2 - 2, middle - 5);
}


static BRect
scroller_right_rect(BRect bounds)
{
	const float kArrowSpan = 10;
	const float kArrowDepth = 5;
	const float middle = bounds.bottom / 2;
	const float center = kScroller / 2;
	return BRect(center - kArrowDepth / 2, middle - kArrowSpan / 2,
		center + kArrowDepth / 2, middle + kArrowSpan / 2);
}


// HaikuControlLook.cpp DrawSliderTriangle. The source rect is the thumb frame;
// the fill shape is built after right--, bottom-- and an InsetBy(1, 1), but
// centerh and centerv are taken before the inset.

static void
haiku_slider_h_original(BRect rect, BPoint points[3])
{
	rect.right--;
	rect.bottom--;
	float centerh = (rect.left + rect.right) / 2;
	rect.InsetBy(1, 1);
	points[0].Set(rect.left, rect.bottom + 1);
	points[1].Set(rect.right + 1, rect.bottom + 1);
	points[2].Set(centerh + 0.5, rect.top);
}


static BRect
haiku_slider_h_rect(BRect rect)
{
	rect.right--;
	rect.bottom--;
	rect.InsetBy(1, 1);
	return BRect(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
}


static void
haiku_slider_v_original(BRect rect, BPoint points[3])
{
	rect.right--;
	rect.bottom--;
	float centerv = (rect.top + rect.bottom) / 2;
	rect.InsetBy(1, 1);
	points[0].Set(rect.right + 1, rect.top);
	points[1].Set(rect.right + 1, rect.bottom + 1);
	points[2].Set(rect.left, centerv + 0.5);
}


static BRect
haiku_slider_v_rect(BRect rect)
{
	rect.right--;
	rect.bottom--;
	rect.InsetBy(1, 1);
	return BRect(rect.left, rect.top, rect.right + 1, rect.bottom + 1);
}


// BeControlLook.cpp DrawSliderTriangle. The fixed apex is left as it is,
// so the 24 unit cases differ on purpose.

static void
be_slider_h_original(BRect rect, BPoint points[3])
{
	points[0].Set(rect.left, rect.bottom - 1);
	points[1].Set(rect.left + 6, rect.top);
	points[2].Set(rect.right, rect.bottom - 1);
}


static BRect
be_slider_h_rect(BRect rect)
{
	return BRect(rect.left, rect.top, rect.right, rect.bottom - 1);
}


static void
be_slider_v_original(BRect rect, BPoint points[3])
{
	points[0].Set(rect.left + 1, rect.top);
	points[1].Set(rect.left + 7, rect.top + 6);
	points[2].Set(rect.left + 1, rect.bottom);
}


static BRect
be_slider_v_rect(BRect rect)
{
	return BRect(rect.left + 1, rect.top, rect.right - 1, rect.bottom);
}


static const ArrowCase kCases[] = {
	{ "Haiku marker 12", BRect(0, 0, 12, 12), BControlLook::B_DOWN_ARROW,
		haiku_marker_original, haiku_marker_rect },
	{ "Haiku marker 24", BRect(0, 0, 24, 24), BControlLook::B_DOWN_ARROW,
		haiku_marker_original, haiku_marker_rect },
	{ "Be marker", BRect(0, 0, 20, 20), BControlLook::B_DOWN_ARROW,
		be_marker_original, be_marker_rect },
	{ "Flat marker", BRect(0, 0, 20, 20), BControlLook::B_DOWN_ARROW,
		flat_marker_original, flat_marker_rect },
	{ "Tracker sort down", BRect(0, 0, 80, 20), BControlLook::B_DOWN_ARROW,
		tracker_down_original, tracker_down_rect },
	{ "Tracker sort up", BRect(0, 0, 80, 20), BControlLook::B_UP_ARROW,
		tracker_up_original, tracker_up_rect },
	{ "Scroller up", BRect(0, 0, 60, 11), BControlLook::B_UP_ARROW,
		scroller_up_original, scroller_up_rect },
	{ "Scroller down", BRect(0, 0, 60, 11), BControlLook::B_DOWN_ARROW,
		scroller_down_original, scroller_down_rect },
	{ "Scroller left", BRect(0, 0, 11, 60), BControlLook::B_LEFT_ARROW,
		scroller_left_original, scroller_left_rect },
	{ "Scroller right", BRect(0, 0, 11, 60), BControlLook::B_RIGHT_ARROW,
		scroller_right_original, scroller_right_rect },
	{ "Haiku slider horiz", BRect(0, 0, 12, 8), BControlLook::B_UP_ARROW,
		haiku_slider_h_original, haiku_slider_h_rect },
	{ "Haiku slider vert", BRect(0, 0, 8, 12), BControlLook::B_LEFT_ARROW,
		haiku_slider_v_original, haiku_slider_v_rect },
	{ "Be slider horiz 12", BRect(0, 0, 12, 8), BControlLook::B_UP_ARROW,
		be_slider_h_original, be_slider_h_rect },
	{ "Be slider vert 12", BRect(0, 0, 8, 12), BControlLook::B_RIGHT_ARROW,
		be_slider_v_original, be_slider_v_rect },
	{ "Be slider horiz 24", BRect(0, 0, 24, 16), BControlLook::B_UP_ARROW,
		be_slider_h_original, be_slider_h_rect },
	{ "Be slider vert 24", BRect(0, 0, 16, 24), BControlLook::B_RIGHT_ARROW,
		be_slider_v_original, be_slider_v_rect }
};

static const int32 kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

static void
sort_points(BPoint points[3])
{
	for (int32 i = 0; i < 2; i++) {
		for (int32 j = 0; j < 2 - i; j++) {
			bool swap = points[j].x > points[j + 1].x
				|| (points[j].x == points[j + 1].x
					&& points[j].y > points[j + 1].y);
			if (swap) {
				BPoint temp = points[j];
				points[j] = points[j + 1];
				points[j + 1] = temp;
			}
		}
	}
}


// the tolerated difference between the old geometry and the new, in pixels
static const float kThreshold = 1.0;
static const int32 kColumns = 4;


// Fills in both point sets and returns the largest coordinate difference
// between them. Zero means GetArrowShape reproduces the original math exactly.
static float
compare_case(const ArrowCase& test, BPoint original[3], BPoint helper[3],
	BRect& shapeRect)
{
	shapeRect = test.ShapeRect(test.source);
	test.Original(test.source, original);
	BControlLook::GetArrowShape(shapeRect, test.direction, helper);

	// the two point orders differ, so sort both before comparing
	sort_points(original);
	sort_points(helper);

	float worst = 0.0;
	for (int32 i = 0; i < 3; i++) {
		float dx = fabsf(original[i].x - helper[i].x);
		float dy = fabsf(original[i].y - helper[i].y);
		if (dx > worst)
			worst = dx;
		if (dy > worst)
			worst = dy;
	}

	return worst;
}


// Draws one case scaled to fill the view, so that shapes of very different
// sizes stay comparable.
class CaseView : public BView {
public:
							CaseView(const ArrowCase& test);

	virtual	void			Draw(BRect updateRect);
	virtual	BSize			MinSize();
	virtual	BSize			MaxSize();
	virtual	BSize			PreferredSize();

			float			Delta() const { return fDelta; }

private:
			BPoint			_Map(BPoint point, BPoint origin);
			void			_DrawTriangle(const BPoint points[3],
								BPoint origin, bool fill);

			BPoint			fOriginal[3];
			BPoint			fHelper[3];
			BRect			fShapeRect;
			BRect			fBounds;
			float			fDelta;
};


CaseView::CaseView(const ArrowCase& test)
	:
	BView("case", B_WILL_DRAW)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fDelta = compare_case(test, fOriginal, fHelper, fShapeRect);

	// everything that gets drawn, which is what the view is sized from
	fBounds = fShapeRect;
	for (int32 i = 0; i < 3; i++) {
		fBounds = fBounds | BRect(fOriginal[i], fOriginal[i]);
		fBounds = fBounds | BRect(fHelper[i], fHelper[i]);
	}
}


BSize
CaseView::PreferredSize()
{
	// drawn at native size, so the view only needs the shape plus a margin
	const float margin = ceilf(be_plain_font->Size() / 2);
	return BSize(ceilf(fBounds.Width()) + margin * 2,
		ceilf(fBounds.Height()) + margin * 2);
}


BSize
CaseView::MinSize()
{
	return PreferredSize();
}


BSize
CaseView::MaxSize()
{
	// stretch sideways with the column, but do not grow taller
	return BSize(B_SIZE_UNLIMITED, PreferredSize().height);
}


BPoint
CaseView::_Map(BPoint point, BPoint origin)
{
	return BPoint(origin.x + point.x, origin.y + point.y);
}


void
CaseView::_DrawTriangle(const BPoint points[3], BPoint origin, bool fill)
{
	BPoint placed[3];
	for (int32 i = 0; i < 3; i++)
		placed[i] = _Map(points[i], origin);

	if (fill)
		FillTriangle(placed[0], placed[1], placed[2]);
	else
		StrokeTriangle(placed[0], placed[1], placed[2]);
}


void
CaseView::Draw(BRect updateRect)
{
	const BRect bounds = fBounds;
	BRect box = Bounds();
	BPoint origin(
		box.left + roundf((box.Width() - bounds.Width()) / 2) - bounds.left,
		box.top + roundf((box.Height() - bounds.Height()) / 2) - bounds.top);

	uint32 flags = Flags();
	SetFlags(flags | B_SUBPIXEL_PRECISE);

	// the rect handed to GetArrowShape, for orientation
	SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
	StrokeRect(BRect(_Map(fShapeRect.LeftTop(), origin),
		_Map(fShapeRect.RightBottom(), origin)));

	// original filled, helper outlined on top
	SetHighColor(230, 120, 120);
	_DrawTriangle(fOriginal, origin, true);

	SetHighColor(0, 0, 200);
	_DrawTriangle(fHelper, origin, false);

	SetFlags(flags);
}


class TestWindow : public BWindow {
public:
							TestWindow();
};


TestWindow::TestWindow()
	:
	BWindow(BRect(100, 100, 100, 100), "ArrowShapeTest", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE)
{
	const rgb_color kExact = { 0, 128, 0, 255 };
	const rgb_color kTolerance = { 150, 90, 0, 255 };
	const rgb_color kFail = { 200, 0, 0, 255 };

	BGridView* grid = new BGridView(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING);

	int32 failures = 0;
	int32 differing = 0;
	for (int32 i = 0; i < kCaseCount; i++) {
		CaseView* caseView = new CaseView(kCases[i]);

		// Check the the change is within tolerance
		const float delta = caseView->Delta();
		char verdict[64];
		rgb_color color;
		if (delta > kThreshold) {
			failures++;
			color = kFail;
			snprintf(verdict, sizeof(verdict), "FAIL  %.2f", delta);
		} else if (delta == 0.0) {
			color = kExact;
			snprintf(verdict, sizeof(verdict), "PASS");
		} else {
			differing++;
			color = kTolerance;
			snprintf(verdict, sizeof(verdict), "PASS  %.2f", delta);
		}

		BStringView* verdictView = new BStringView("verdict", verdict);
		verdictView->SetHighColor(color);

		// the box owns the title, so it keeps the label clear of the border
		BBox* box = new BBox("case");
		box->SetLabel(kCases[i].name);
		box->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(caseView)
			.Add(verdictView)
			.SetInsets(B_USE_SMALL_INSETS)
			.View());

		grid->GridLayout()->AddView(box, i % kColumns, i / kColumns);
	}

	char summary[160];
	if (failures == 0) {
		snprintf(summary, sizeof(summary), "%" B_PRId32 " cases, %" B_PRId32
			" unchanged, %" B_PRId32 " within %g px", kCaseCount,
			kCaseCount - differing, differing, kThreshold);
	} else {
		snprintf(summary, sizeof(summary), "%" B_PRId32 " cases, %" B_PRId32
			" over the %g px threshold", kCaseCount, failures, kThreshold);
	}

	BStringView* summaryView = new BStringView("summary", summary);
	summaryView->SetHighColor(failures == 0 ? kExact : kFail);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(summaryView)
		.Add(new BStringView("legend", "red fill = original, blue "
			"outline = GetArrowShape, grey = rect passed in"))
		.Add(grid)
		.End();
}


int
main()
{
	BApplication app("application/x-vnd.Vitruvian-ArrowShapeTest");

	TestWindow* window = new TestWindow();
	window->Show();

	app.Run();
	return 0;
}
