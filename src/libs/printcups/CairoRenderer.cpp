/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * CairoRenderer: play Haiku BPicture streams onto a cairo PDF surface.
 * Ops that cannot be mapped fall back to a white placeholder or a
 * BBitmap image when one is provided.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <DataIO.h>
#include <List.h>
#include <Picture.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>

#include <cairo/cairo.h>
#include <cairo/cairo-pdf.h>

#include <PicturePlayer.h>
#include <Shape.h>

#include "PrintCupsRender.h"
#include "printcups.h"


struct CairoState {
	cairo_t*	cairo;
	float		scale;
	float		penX;
	float		penY;
	float		originX;
	float		originY;
	rgb_color	foreColor;
	rgb_color	backColor;
	float		penSize;
	drawing_mode drawingMode;
	float		fontSize;
};


static double
ToX(const CairoState* s, float x)
{
	return (x - s->originX) * s->scale;
}


static double
ToY(const CairoState* s, float y)
{
	// BeOS y grows downward; the page transform flips y at show_page time.
	return (y - s->originY) * s->scale;
}


static void
SetSourceFore(CairoState* s)
{
	cairo_set_source_rgba(s->cairo,
		s->foreColor.red / 255.0, s->foreColor.green / 255.0,
		s->foreColor.blue / 255.0, s->foreColor.alpha / 255.0);
}


// ----- picture_player_callbacks_compat implementation -----

static void
cb_nop(void* user)
{
	(void)user;
}


static void
cb_move_pen_by(void* user, BPoint delta)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->penX += delta.x;
	s->penY += delta.y;
}


static void
cb_stroke_line(void* user, BPoint start, BPoint end)
{
	CairoState* s = static_cast<CairoState*>(user);
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_move_to(s->cairo, ToX(s, start.x), ToY(s, start.y));
	cairo_line_to(s->cairo, ToX(s, end.x), ToY(s, end.y));
	cairo_stroke(s->cairo);
	s->penX = end.x;
	s->penY = end.y;
}


static void
cb_stroke_rect(void* user, BRect rect)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_rectangle(s->cairo, ToX(s, rect.left), ToY(s, rect.top),
		rect.Width() * s->scale, rect.Height() * s->scale);
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_stroke(s->cairo);
}


static void
cb_fill_rect(void* user, BRect rect)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_rectangle(s->cairo, ToX(s, rect.left), ToY(s, rect.top),
		rect.Width() * s->scale, rect.Height() * s->scale);
	SetSourceFore(s);
	cairo_fill(s->cairo);
}


static void
cb_stroke_round_rect(void* user, BRect rect, BPoint radii)
{
	CairoState* s = static_cast<CairoState*>(user);
	float rx = radii.x * s->scale;
	float ry = radii.y * s->scale;
	if (rx < 0.5f)
		rx = 0.5f;
	if (ry < 0.5f)
		ry = 0.5f;

	double x = ToX(s, rect.left);
	double y = ToY(s, rect.top);
	double w = rect.Width() * s->scale;
	double h = rect.Height() * s->scale;
	if (rx > w / 2)
		rx = w / 2;
	if (ry > h / 2)
		ry = h / 2;

	cairo_new_sub_path(s->cairo);
	cairo_arc(s->cairo, x + rx, y + ry, rx, M_PI, 1.5 * M_PI);
	cairo_arc(s->cairo, x + w - rx, y + ry, rx, 1.5 * M_PI, 2.0 * M_PI);
	cairo_arc(s->cairo, x + w - rx, y + h - ry, rx, 0, 0.5 * M_PI);
	cairo_arc(s->cairo, x + rx, y + h - ry, rx, 0.5 * M_PI, M_PI);
	cairo_close_path(s->cairo);
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_stroke(s->cairo);
}


static void
cb_fill_round_rect(void* user, BRect rect, BPoint radii)
{
	CairoState* s = static_cast<CairoState*>(user);
	// Reuse stroke path then fill.
	cb_stroke_round_rect(user, rect, radii);
	// stroke_round_rect already stroked; draw fill path again
	float rx = radii.x * s->scale;
	float ry = radii.y * s->scale;
	if (rx < 0.5f)
		rx = 0.5f;
	if (ry < 0.5f)
		ry = 0.5f;
	double x = ToX(s, rect.left);
	double y = ToY(s, rect.top);
	double w = rect.Width() * s->scale;
	double h = rect.Height() * s->scale;
	if (rx > w / 2)
		rx = w / 2;
	if (ry > h / 2)
		ry = h / 2;
	cairo_new_sub_path(s->cairo);
	cairo_arc(s->cairo, x + rx, y + ry, rx, M_PI, 1.5 * M_PI);
	cairo_arc(s->cairo, x + w - rx, y + ry, rx, 1.5 * M_PI, 2.0 * M_PI);
	cairo_arc(s->cairo, x + w - rx, y + h - ry, rx, 0, 0.5 * M_PI);
	cairo_arc(s->cairo, x + rx, y + h - ry, rx, 0.5 * M_PI, M_PI);
	cairo_close_path(s->cairo);
	SetSourceFore(s);
	cairo_fill(s->cairo);
}


static void
cb_stroke_bezier(void* user, BPoint* control)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_move_to(s->cairo, ToX(s, control[0].x), ToY(s, control[0].y));
	cairo_curve_to(s->cairo,
		ToX(s, control[1].x), ToY(s, control[1].y),
		ToX(s, control[2].x), ToY(s, control[2].y),
		ToX(s, control[3].x), ToY(s, control[3].y));
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_stroke(s->cairo);
}


static void
cb_fill_bezier(void* user, BPoint* control)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_move_to(s->cairo, ToX(s, control[0].x), ToY(s, control[0].y));
	cairo_curve_to(s->cairo,
		ToX(s, control[1].x), ToY(s, control[1].y),
		ToX(s, control[2].x), ToY(s, control[2].y),
		ToX(s, control[3].x), ToY(s, control[3].y));
	SetSourceFore(s);
	cairo_fill(s->cairo);
}


static void
cb_stroke_arc(void* user, BPoint center, BPoint radii, float startTheta,
	float arcTheta)
{
	CairoState* s = static_cast<CairoState*>(user);
	double cx = ToX(s, center.x);
	double cy = ToY(s, center.y);
	double rx = radii.x * s->scale;
	double ry = radii.y * s->scale;
	double cairoStart = -startTheta * 180.0 / M_PI;
	double cairoExtent = -arcTheta * 180.0 / M_PI;
	cairo_save(s->cairo);
	cairo_translate(s->cairo, cx, cy);
	cairo_scale(s->cairo, rx, ry);
	cairo_arc(s->cairo, 0, 0, 1.0, cairoStart * M_PI / 180.0,
		(cairoStart + cairoExtent) * M_PI / 180.0);
	cairo_restore(s->cairo);
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_stroke(s->cairo);
}


static void
cb_fill_arc(void* user, BPoint center, BPoint radii, float startTheta,
	float arcTheta)
{
	CairoState* s = static_cast<CairoState*>(user);
	double cx = ToX(s, center.x);
	double cy = ToY(s, center.y);
	double rx = radii.x * s->scale;
	double ry = radii.y * s->scale;
	double cairoStart = -startTheta * 180.0 / M_PI;
	double cairoExtent = -arcTheta * 180.0 / M_PI;
	cairo_save(s->cairo);
	cairo_translate(s->cairo, cx, cy);
	cairo_scale(s->cairo, rx, ry);
	cairo_arc(s->cairo, 0, 0, 1.0, cairoStart * M_PI / 180.0,
		(cairoStart + cairoExtent) * M_PI / 180.0);
	cairo_restore(s->cairo);
	SetSourceFore(s);
	cairo_fill(s->cairo);
}


static void
cb_stroke_ellipse(void* user, BPoint center, BPoint radii)
{
	CairoState* s = static_cast<CairoState*>(user);
	double cx = ToX(s, center.x);
	double cy = ToY(s, center.y);
	double rx = radii.x * s->scale;
	double ry = radii.y * s->scale;
	cairo_save(s->cairo);
	cairo_translate(s->cairo, cx, cy);
	cairo_scale(s->cairo, rx, ry);
	cairo_arc(s->cairo, 0, 0, 1.0, 0, 2 * M_PI);
	cairo_restore(s->cairo);
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_stroke(s->cairo);
}


static void
cb_fill_ellipse(void* user, BPoint center, BPoint radii)
{
	CairoState* s = static_cast<CairoState*>(user);
	double cx = ToX(s, center.x);
	double cy = ToY(s, center.y);
	double rx = radii.x * s->scale;
	double ry = radii.y * s->scale;
	cairo_save(s->cairo);
	cairo_translate(s->cairo, cx, cy);
	cairo_scale(s->cairo, rx, ry);
	cairo_arc(s->cairo, 0, 0, 1.0, 0, 2 * M_PI);
	cairo_restore(s->cairo);
	SetSourceFore(s);
	cairo_fill(s->cairo);
}


static void
cb_stroke_polygon(void* user, int32 numPoints, const BPoint* points,
	bool isClosed)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (numPoints <= 0 || points == NULL)
		return;

	cairo_move_to(s->cairo, ToX(s, points[0].x), ToY(s, points[0].y));
	for (int32 i = 1; i < numPoints; i++)
		cairo_line_to(s->cairo, ToX(s, points[i].x), ToY(s, points[i].y));
	if (isClosed)
		cairo_close_path(s->cairo);
	SetSourceFore(s);
	cairo_set_line_width(s->cairo, s->penSize * s->scale);
	cairo_stroke(s->cairo);
}


static void
cb_fill_polygon(void* user, int32 numPoints, const BPoint* points,
	bool isClosed)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (numPoints <= 0 || points == NULL)
		return;

	cairo_move_to(s->cairo, ToX(s, points[0].x), ToY(s, points[0].y));
	for (int32 i = 1; i < numPoints; i++)
		cairo_line_to(s->cairo, ToX(s, points[i].x), ToY(s, points[i].y));
	if (isClosed)
		cairo_close_path(s->cairo);
	SetSourceFore(s);
	cairo_fill(s->cairo);
}


static void
cb_draw_string(void* user, const char* string, float deltax, float deltay)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (string == NULL || string[0] == '\0')
		return;

	cairo_select_font_face(s->cairo, "sans-serif",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(s->cairo, s->fontSize * s->scale);
	SetSourceFore(s);
	cairo_move_to(s->cairo, ToX(s, s->penX), ToY(s, s->penY));
	cairo_show_text(s->cairo, string);

	cairo_text_extents_t extents;
	cairo_text_extents(s->cairo, string, &extents);
	if (s->scale > 0)
		s->penX += extents.x_advance / s->scale;
	s->penX += deltax;
	s->penY += deltay;
}


static void
cb_draw_pixels(void* user, BRect src, BRect dest, int32 width, int32 height,
	int32 bytesPerRow, int32 pixelFormat, int32 flags, const void* data)
{
	(void)user;
	(void)src;
	(void)width;
	(void)height;
	(void)bytesPerRow;
	(void)pixelFormat;
	(void)flags;
	(void)data;
	// Unmapped: leave dest as a light grey placeholder so layout stays
	// visible. BBitmap fallback is used when PrintJob supplies one.
	CairoState* s = static_cast<CairoState*>(user);
	cairo_set_source_rgb(s->cairo, 0.85, 0.85, 0.85);
	cairo_rectangle(s->cairo, ToX(s, dest.left), ToY(s, dest.top),
		dest.Width() * s->scale, dest.Height() * s->scale);
	cairo_fill(s->cairo);
}


static void
cb_set_clipping_rects(void* user, const BRect* rects, uint32 numRects)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (rects == NULL || numRects == 0)
		return;

	cairo_reset_clip(s->cairo);
	for (uint32 i = 0; i < numRects; i++) {
		cairo_rectangle(s->cairo, ToX(s, rects[i].left), ToY(s, rects[i].top),
			rects[i].Width() * s->scale, rects[i].Height() * s->scale);
	}
	cairo_clip(s->cairo);
}


static void
cb_push_state(void* user)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_save(s->cairo);
}


static void
cb_pop_state(void* user)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_restore(s->cairo);
}


static void
cb_enter_state_change(void* user)
{
	(void)user;
}


static void
cb_exit_state_change(void* user)
{
	(void)user;
}


static void
cb_enter_font_state(void* user)
{
	(void)user;
}


static void
cb_exit_font_state(void* user)
{
	(void)user;
}


static void
cb_set_origin(void* user, BPoint pt)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->originX = pt.x;
	s->originY = pt.y;
}


static void
cb_set_pen_location(void* user, BPoint pt)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->penX = pt.x;
	s->penY = pt.y;
}


static void
cb_set_drawing_mode(void* user, drawing_mode mode)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->drawingMode = mode;
	if (mode == B_OP_ERASE)
		cairo_set_operator(s->cairo, CAIRO_OPERATOR_CLEAR);
	else if (mode == B_OP_INVERT)
		cairo_set_operator(s->cairo, CAIRO_OPERATOR_DIFFERENCE);
	else
		cairo_set_operator(s->cairo, CAIRO_OPERATOR_OVER);
}


static void
cb_set_line_mode(void* user, cap_mode capMode, join_mode joinMode,
	float miterLimit)
{
	CairoState* s = static_cast<CairoState*>(user);
	(void)miterLimit;
	if (capMode == B_BUTT_CAP)
		cairo_set_line_cap(s->cairo, CAIRO_LINE_CAP_BUTT);
	else if (capMode == B_ROUND_CAP)
		cairo_set_line_cap(s->cairo, CAIRO_LINE_CAP_ROUND);
	else if (capMode == B_SQUARE_CAP)
		cairo_set_line_cap(s->cairo, CAIRO_LINE_CAP_SQUARE);

	if (joinMode == B_MITER_JOIN)
		cairo_set_line_join(s->cairo, CAIRO_LINE_JOIN_MITER);
	else if (joinMode == B_ROUND_JOIN)
		cairo_set_line_join(s->cairo, CAIRO_LINE_JOIN_ROUND);
	else if (joinMode == B_BEVEL_JOIN)
		cairo_set_line_join(s->cairo, CAIRO_LINE_JOIN_BEVEL);
}


static void
cb_set_pen_size(void* user, float size)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->penSize = size > 0 ? size : 1.0f;
}


static void
cb_set_fore_color(void* user, rgb_color color)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->foreColor = color;
}


static void
cb_set_back_color(void* user, rgb_color color)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->backColor = color;
}


static void
cb_set_stipple_pattern(void* user, pattern p)
{
	(void)user;
	(void)p;
}


static void
cb_set_scale(void* user, float scale)
{
	CairoState* s = static_cast<CairoState*>(user);
	(void)scale;
	// Picture scale is handled by the page transform; ignore per-picture.
}


static void
cb_set_font_family(void* user, const char* family)
{
	(void)user;
	(void)family;
}


static void
cb_set_font_style(void* user, const char* style)
{
	(void)user;
	(void)style;
}


static void
cb_set_font_spacing(void* user, int32 spacing)
{
	(void)user;
	(void)spacing;
}


static void
cb_set_font_size(void* user, float size)
{
	CairoState* s = static_cast<CairoState*>(user);
	s->fontSize = size > 0 ? size : 12.0f;
}


static void
cb_set_font_rotate(void* user, float rotation)
{
	(void)user;
	(void)rotation;
}


static void
cb_set_font_encoding(void* user, int32 encoding)
{
	(void)user;
	(void)encoding;
}


static void
cb_set_font_flags(void* user, int32 flags)
{
	(void)user;
	(void)flags;
}


static void
cb_set_font_shear(void* user, float shear)
{
	(void)user;
	(void)shear;
}


static void
cb_set_font_bpp(void* user, int32 bpp)
{
	(void)user;
	(void)bpp;
}


static void
cb_set_font_face(void* user, int32 face)
{
	(void)user;
	(void)face;
}


static void
cb_set_blending_mode(void* user, source_alpha alphaSrcMode,
	alpha_function alphaFncMode)
{
	(void)user;
	(void)alphaSrcMode;
	(void)alphaFncMode;
}


static void
cb_set_transform(void* user, const BAffineTransform& transform)
{
	(void)user;
	(void)transform;
}


static void
cb_translate_by(void* user, double x, double y)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_translate(s->cairo, x * s->scale, y * s->scale);
}


static void
cb_scale_by(void* user, double x, double y)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_scale(s->cairo, x, y);
}


static void
cb_rotate_by(void* user, double angleRadians)
{
	CairoState* s = static_cast<CairoState*>(user);
	cairo_rotate(s->cairo, angleRadians);
}


static void
cb_blend_layer(void* user, class Layer* layer)
{
	(void)user;
	(void)layer;
}


static void
cb_clip_to_rect(void* user, const BRect& rect, bool inverse)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (inverse)
		return;

	cairo_rectangle(s->cairo, ToX(s, rect.left), ToY(s, rect.top),
		rect.Width() * s->scale, rect.Height() * s->scale);
	cairo_clip(s->cairo);
}


static void
cb_clip_to_shape(void* user, int32 opCount, const uint32 opList[],
	int32 ptCount, const BPoint ptList[], bool inverse)
{
	(void)user;
	(void)opCount;
	(void)opList;
	(void)ptCount;
	(void)ptList;
	(void)inverse;
}


static void
cb_draw_string_locations(void* user, const char* string,
	const BPoint* locations, size_t locationCount)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (string == NULL || locations == NULL)
		return;

	cairo_select_font_face(s->cairo, "sans-serif",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(s->cairo, s->fontSize * s->scale);
	SetSourceFore(s);
	for (size_t i = 0; i < locationCount; i++) {
		cairo_move_to(s->cairo, ToX(s, locations[i].x), ToY(s, locations[i].y));
		cairo_show_text(s->cairo, string);
	}
}


// Gradients and shapes are unmapped, so fill with the fore colour to keep the content.
static void
cb_fill_rect_gradient(void* user, BRect rect, const BGradient& gradient)
{
	(void)gradient;
	cb_fill_rect(user, rect);
}


static void
cb_stroke_rect_gradient(void* user, BRect rect, const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_rect(user, rect);
}


static void
cb_fill_round_rect_gradient(void* user, BRect rect, BPoint radii,
	const BGradient& gradient)
{
	(void)gradient;
	cb_fill_round_rect(user, rect, radii);
}


static void
cb_stroke_round_rect_gradient(void* user, BRect rect, BPoint radii,
	const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_round_rect(user, rect, radii);
}


static void
cb_fill_bezier_gradient(void* user, const BPoint* points,
	const BGradient& gradient)
{
	(void)gradient;
	cb_fill_bezier(user, const_cast<BPoint*>(points));
}


static void
cb_stroke_bezier_gradient(void* user, const BPoint* points,
	const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_bezier(user, const_cast<BPoint*>(points));
}


static void
cb_fill_arc_gradient(void* user, BPoint center, BPoint radii,
	float startTheta, float arcTheta, const BGradient& gradient)
{
	(void)gradient;
	cb_fill_arc(user, center, radii, startTheta, arcTheta);
}


static void
cb_stroke_arc_gradient(void* user, BPoint center, BPoint radii,
	float startTheta, float arcTheta, const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_arc(user, center, radii, startTheta, arcTheta);
}


static void
cb_fill_ellipse_gradient(void* user, BRect rect, const BGradient& gradient)
{
	(void)gradient;
	cb_fill_ellipse(user,
		BPoint(rect.left + rect.Width() / 2, rect.top + rect.Height() / 2),
		BPoint(rect.Width() / 2, rect.Height() / 2));
}


static void
cb_stroke_ellipse_gradient(void* user, BRect rect, const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_ellipse(user,
		BPoint(rect.left + rect.Width() / 2, rect.top + rect.Height() / 2),
		BPoint(rect.Width() / 2, rect.Height() / 2));
}


static void
cb_fill_polygon_gradient(void* user, int32 numPoints, const BPoint* points,
	bool isClosed, const BGradient& gradient)
{
	(void)gradient;
	cb_fill_polygon(user, numPoints, points, isClosed);
}


static void
cb_stroke_polygon_gradient(void* user, int32 numPoints, const BPoint* points,
	bool isClosed, const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_polygon(user, numPoints, points, isClosed);
}


static void
cb_fill_shape_gradient(void* user, BShape shape, const BGradient& gradient)
{
	(void)user;
	(void)shape;
	(void)gradient;
}


static void
cb_stroke_shape_gradient(void* user, BShape shape, const BGradient& gradient)
{
	(void)user;
	(void)shape;
	(void)gradient;
}


static void
cb_set_fill_rule(void* user, int32 fillRule)
{
	CairoState* s = static_cast<CairoState*>(user);
	if (fillRule == B_EVEN_ODD)
		cairo_set_fill_rule(s->cairo, CAIRO_FILL_RULE_EVEN_ODD);
	else
		cairo_set_fill_rule(s->cairo, CAIRO_FILL_RULE_WINDING);
}


static void
cb_stroke_line_gradient(void* user, BPoint start, BPoint end,
	const BGradient& gradient)
{
	(void)gradient;
	cb_stroke_line(user, start, end);
}


static void
cb_stroke_shape(void* user, const BShape* shape)
{
	(void)user;
	(void)shape;
}


static void
cb_fill_shape(void* user, const BShape* shape)
{
	(void)user;
	(void)shape;
}


static void
cb_draw_picture(void* user, BPoint where, int32 token)
{
	(void)user;
	(void)where;
	(void)token;
	// Nested picture playback is handled by PicturePlayer via the
	// pictures list passed to the player; this op is a no-op marker.
}


static void
cb_clip_to_picture(void* user, int32 token, BPoint point,
	bool clip_to_inverse_picture)
{
	(void)user;
	(void)token;
	(void)point;
	(void)clip_to_inverse_picture;
}


static void
InstallCallbackTable(void** table)
{
	// Layout must match BPrivate::picture_player_callbacks_compat.
	void** t = table;
	t[0] = (void*)cb_nop;
	t[1] = (void*)cb_move_pen_by;
	t[2] = (void*)cb_stroke_line;
	t[3] = (void*)cb_stroke_rect;
	t[4] = (void*)cb_fill_rect;
	t[5] = (void*)cb_stroke_round_rect;
	t[6] = (void*)cb_fill_round_rect;
	t[7] = (void*)cb_stroke_bezier;
	t[8] = (void*)cb_fill_bezier;
	t[9] = (void*)cb_stroke_arc;
	t[10] = (void*)cb_fill_arc;
	t[11] = (void*)cb_stroke_ellipse;
	t[12] = (void*)cb_fill_ellipse;
	t[13] = (void*)cb_stroke_polygon;
	t[14] = (void*)cb_fill_polygon;
	t[15] = (void*)cb_stroke_shape;
	t[16] = (void*)cb_fill_shape;
	t[17] = (void*)cb_draw_string;
	t[18] = (void*)cb_draw_pixels;
	t[19] = (void*)cb_draw_picture;
	t[20] = (void*)cb_set_clipping_rects;
	t[21] = (void*)cb_clip_to_picture;
	t[22] = (void*)cb_push_state;
	t[23] = (void*)cb_pop_state;
	t[24] = (void*)cb_enter_state_change;
	t[25] = (void*)cb_exit_state_change;
	t[26] = (void*)cb_enter_font_state;
	t[27] = (void*)cb_exit_font_state;
	t[28] = (void*)cb_set_origin;
	t[29] = (void*)cb_set_pen_location;
	t[30] = (void*)cb_set_drawing_mode;
	t[31] = (void*)cb_set_line_mode;
	t[32] = (void*)cb_set_pen_size;
	t[33] = (void*)cb_set_fore_color;
	t[34] = (void*)cb_set_back_color;
	t[35] = (void*)cb_set_stipple_pattern;
	t[36] = (void*)cb_set_scale;
	t[37] = (void*)cb_set_font_family;
	t[38] = (void*)cb_set_font_style;
	t[39] = (void*)cb_set_font_spacing;
	t[40] = (void*)cb_set_font_size;
	t[41] = (void*)cb_set_font_rotate;
	t[42] = (void*)cb_set_font_encoding;
	t[43] = (void*)cb_set_font_flags;
	t[44] = (void*)cb_set_font_shear;
	t[45] = (void*)cb_set_font_bpp;
	t[46] = (void*)cb_set_font_face;
	t[47] = (void*)cb_set_blending_mode;
	t[48] = (void*)cb_set_transform;
	t[49] = (void*)cb_translate_by;
	t[50] = (void*)cb_scale_by;
	t[51] = (void*)cb_rotate_by;
	t[52] = (void*)cb_blend_layer;
	t[53] = (void*)cb_clip_to_rect;
	t[54] = (void*)cb_clip_to_shape;
	t[55] = (void*)cb_draw_string_locations;
	t[56] = (void*)cb_fill_rect_gradient;
	t[57] = (void*)cb_stroke_rect_gradient;
	t[58] = (void*)cb_fill_round_rect_gradient;
	t[59] = (void*)cb_stroke_round_rect_gradient;
	t[60] = (void*)cb_fill_bezier_gradient;
	t[61] = (void*)cb_stroke_bezier_gradient;
	t[62] = (void*)cb_fill_arc_gradient;
	t[63] = (void*)cb_stroke_arc_gradient;
	t[64] = (void*)cb_fill_ellipse_gradient;
	t[65] = (void*)cb_stroke_ellipse_gradient;
	t[66] = (void*)cb_fill_polygon_gradient;
	t[67] = (void*)cb_stroke_polygon_gradient;
	t[68] = (void*)cb_fill_shape_gradient;
	t[69] = (void*)cb_stroke_shape_gradient;
	t[70] = (void*)cb_set_fill_rule;
	t[71] = (void*)cb_stroke_line_gradient;
}


static void
InitCairoState(CairoState* s, cairo_t* cairo, float scale)
{
	memset(s, 0, sizeof(*s));
	s->cairo = cairo;
	s->scale = scale > 0 ? scale : 1.0f;
	s->foreColor.red = 0;
	s->foreColor.green = 0;
	s->foreColor.blue = 0;
	s->foreColor.alpha = 255;
	s->backColor.red = 255;
	s->backColor.green = 255;
	s->backColor.blue = 255;
	s->backColor.alpha = 255;
	s->penSize = 1.0f;
	s->drawingMode = B_OP_COPY;
	s->fontSize = 12.0f;
}


status_t
printcups_render_pages_pdf(const char* pdfPath, BList* pages,
	float paperWidth, float paperHeight, float scale, const char* title)
{
	if (pdfPath == NULL || pages == NULL)
		return B_BAD_VALUE;

	if (scale <= 0.0f)
		scale = 1.0f;

	cairo_surface_t* surface = cairo_pdf_surface_create(pdfPath,
		paperWidth * scale, paperHeight * scale);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return B_ERROR;
	}

	// The PostScript filters carry this into %%Title, which is what
	// cups-pdf names its output after.
	if (title != NULL && title[0] != '\0') {
		cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_TITLE,
			title);
	}

	cairo_t* cairo = cairo_create(surface);
	if (cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(cairo);
		cairo_surface_destroy(surface);
		return B_ERROR;
	}

	if (pages->CountItems() == 0) {
		cairo_destroy(cairo);
		cairo_surface_destroy(surface);
		return B_ERROR;
	}

	void* callbacks[72];
	InstallCallbackTable(callbacks);

	for (int32 i = 0; i < pages->CountItems(); i++) {
		SpooledPage* page = static_cast<SpooledPage*>(pages->ItemAt(i));
		if (page == NULL)
			continue;

		if (i > 0)
			cairo_show_page(cairo);

		// Flip BeOS y-down into PDF y-up.
		cairo_save(cairo);
		cairo_translate(cairo, 0, paperHeight * scale);
		cairo_scale(cairo, scale, -scale);

		cairo_set_source_rgb(cairo, 1, 1, 1);
		cairo_paint(cairo);

		CairoState state;
		InitCairoState(&state, cairo, scale);

		for (int32 p = 0; p < page->pictures.CountItems(); p++) {
			SpooledPicture* pic =
				static_cast<SpooledPicture*>(page->pictures.ItemAt(p));
			if (pic == NULL || pic->picture == NULL)
				continue;

			cairo_save(cairo);
			cairo_translate(cairo, pic->where.x * scale, pic->where.y * scale);

			state.originX = 0;
			state.originY = 0;
			state.penX = 0;
			state.penY = 0;

			pic->picture->Play(callbacks, 72, &state);

			cairo_restore(cairo);
		}

		cairo_restore(cairo);
	}

	cairo_surface_finish(surface);
	cairo_destroy(cairo);
	cairo_surface_destroy(surface);
	return B_OK;
}


status_t
printcups_render_bitmap_fallback(const char* pdfPath, BBitmap* bitmap,
	float paperWidth, float paperHeight, float scale)
{
	if (pdfPath == NULL || bitmap == NULL)
		return B_BAD_VALUE;

	if (scale <= 0.0f)
		scale = 1.0f;

	cairo_surface_t* surface = cairo_pdf_surface_create(pdfPath,
		paperWidth * scale, paperHeight * scale);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return B_ERROR;
	}

	cairo_t* cairo = cairo_create(surface);
	cairo_show_page(cairo);
	cairo_save(cairo);
	cairo_translate(cairo, 0, paperHeight * scale);
	cairo_scale(cairo, scale, -scale);

	int32 w = bitmap->Bounds().IntegerWidth() + 1;
	int32 h = bitmap->Bounds().IntegerHeight() + 1;
	if (w <= 0 || h <= 0) {
		cairo_restore(cairo);
		cairo_destroy(cairo);
		cairo_surface_destroy(surface);
		return B_BAD_VALUE;
	}

	cairo_surface_t* image = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, w, h);
	unsigned char* base = cairo_image_surface_get_data(image);
	int32 stride = cairo_image_surface_get_stride(image);

	for (int32 y = 0; y < h; y++) {
		const rgb_color* row = static_cast<const rgb_color*>(
			bitmap->Bits() + y * bitmap->BytesPerRow());
		for (int32 x = 0; x < w; x++) {
			uint32* dst = reinterpret_cast<uint32*>(base + y * stride + x * 4);
			*dst = (0xffu << 24) | ((uint32)row[x].red << 16)
				| ((uint32)row[x].green << 8) | (uint32)row[x].blue;
		}
	}

	cairo_surface_mark_dirty(image);
	cairo_set_source_surface(cairo, image, 0, 0);
	cairo_paint(cairo);

	cairo_surface_destroy(image);
	cairo_restore(cairo);
	cairo_surface_finish(surface);
	cairo_destroy(cairo);
	cairo_surface_destroy(surface);
	return B_OK;
}
