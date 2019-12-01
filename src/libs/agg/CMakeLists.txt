find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})

AddOn(
	agg
	STATIC

	SOURCES
	src/agg_arc.cpp
	src/agg_arrowhead.cpp
	src/agg_bezier_arc.cpp
	src/agg_bspline.cpp
	src/agg_curves.cpp
	src/agg_embedded_raster_fonts.cpp
	src/agg_gsv_text.cpp
	src/agg_image_filters.cpp
	src/agg_line_aa_basics.cpp
	src/agg_line_profile_aa.cpp
	src/agg_rounded_rect.cpp
	src/agg_sqrt_tables.cpp
	src/agg_trans_affine.cpp
	src/agg_trans_double_path.cpp
	src/agg_trans_single_path.cpp
	src/agg_trans_warp_magnifier.cpp
	src/agg_vcgen_bspline.cpp
	src/agg_vcgen_contour.cpp
	src/agg_vcgen_dash.cpp
	src/agg_vcgen_markers_term.cpp
	src/agg_vcgen_smooth_poly1.cpp
	src/agg_vcgen_stroke.cpp
	src/agg_vpgen_clip_polygon.cpp
	src/agg_vpgen_clip_polyline.cpp
	src/agg_vpgen_segmentator.cpp

	font_freetype/agg_font_freetype.cpp
)
