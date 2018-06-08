#ifndef _PICTUREPROTOCOL_H
#define _PICTUREPROTOCOL_H

// BView dirty bits
enum {
	B_PEN_SIZE_BIT			= 0x00000001,
	B_PEN_LOCATION_BIT		= 0x00000002,
	B_HIGH_COLOR_BIT		= 0x00000004,
	B_LOW_COLOR_BIT			= 0x00000008,
	B_DRAW_MODE_BIT			= 0x00000010,
	B_LINE_MODE_BIT			= 0x00000020,
	B_ORIGIN_BIT			= 0x00000040,
	B_FONT_BIT				= 0x00000080,
	B_PATTERN_BIT			= 0x00000100,
	B_BLEND_MODE_BIT		= 0x00000200
};

// AppServer protocol
enum {

// Picture instructions

	B_PIC_MOVE_PEN_BY			= 0x0010,
	B_PIC_STROKE_LINE			= 0x0100,
	B_PIC_STROKE_RECT			= 0x0101,
	B_PIC_FILL_RECT				= 0x0102,
	B_PIC_STROKE_ROUND_RECT		= 0x0103,
	B_PIC_FILL_ROUND_RECT		= 0x0104,
	B_PIC_STROKE_BEZIER			= 0x0105,
	B_PIC_FILL_BEZIER			= 0x0106,
	B_PIC_STROKE_POLYGON		= 0x010B,
	B_PIC_FILL_POLYGON			= 0x010C,
	B_PIC_STROKE_SHAPE			= 0x010D,
	B_PIC_FILL_SHAPE			= 0x010E,
	B_PIC_DRAW_STRING			= 0x010F,
	B_PIC_DRAW_PIXELS			= 0x0110,
	B_PIC_DRAW_PICTURE			= 0x0112,
	B_PIC_STROKE_ARC			= 0x0113,
	B_PIC_FILL_ARC				= 0x0114,
	B_PIC_STROKE_ELLIPSE		= 0x0115,
	B_PIC_FILL_ELLIPSE			= 0x0116,
	B_PIC_ENTER_STATE_CHANGE	= 0x0200,
	B_PIC_SET_CLIPPING_RECTS	= 0x0201,
	B_PIC_CLIP_TO_PICTURE		= 0x0202,
	B_PIC_PUSH_STATE			= 0x0203,
	B_PIC_POP_STATE				= 0x0204,
	B_PIC_CLEAR_CLIPPING_RECTS	= 0x0205,
	B_PIC_SET_ORIGIN			= 0x0300,
	B_PIC_SET_PEN_LOCATION		= 0x0301,
	B_PIC_SET_DRAWING_MODE		= 0x0302,
	B_PIC_SET_LINE_MODE			= 0x0303,
	B_PIC_SET_PEN_SIZE			= 0x0304,
	B_PIC_SET_SCALE				= 0x0305,
	B_PIC_SET_FORE_COLOR		= 0x0306,
	B_PIC_SET_BACK_COLOR		= 0x0307,
	B_PIC_SET_STIPLE_PATTERN	= 0x0308,
	B_PIC_ENTER_FONT_STATE		= 0x0309,
	B_PIC_SET_BLENDING_MODE		= 0x030A,
	B_PIC_SET_FONT_FAMILY		= 0x0380,
	B_PIC_SET_FONT_STYLE		= 0x0381,
	B_PIC_SET_FONT_SPACING		= 0x0382,
	B_PIC_SET_FONT_ENCODING		= 0x0383,
	B_PIC_SET_FONT_FLAGS		= 0x0384,
	B_PIC_SET_FONT_SIZE			= 0x0385,
	B_PIC_SET_FONT_ROTATE		= 0x0386,
	B_PIC_SET_FONT_SHEAR		= 0x0387,
	B_PIC_SET_FONT_BPP			= 0x0388,
	B_PIC_SET_FONT_FACE			= 0x0389,

// View instructions

	B_VIEW_CREATE				= 0x0500,
	B_VIEW_MOVE_BY				= 0x0501,
//	B_VIEW_MOVE_TO				= 0x0502,	// ????
	B_VIEW_RESIZE_BY			= 0x0503,
	B_VIEW_RESIZE_TO			= 0x0504,
	B_VIEW_DELETE				= 0x0505,
	B_VIEW_FIND					= 0x0506,
	B_VIEW_BOUNDS				= 0x0507,	// ????
	B_VIEW_SET_FLAGS			= 0x0508,
	B_VIEW_SET_EVENT_MASK		= 0x0509,
	B_VIEW_SET_MOUSE_EVENT_MASK	= 0x050A,
	B_VIEW_MOVE_TO				= 0x050B,	// ????
	B_VIEW_SET_VIEW_CURSOR		= 0x050C,

// Window instructions

//	B_WINDOW_MOVE_BY				= 0x0580,
//	B_WINDOW_MOVE_TO				= 0x0581,
	B_WINDOW_RESIZE_BY				= 0x0582,
	B_WINDOW_RESIZE_TO				= 0x0583,
	B_WINDOW_ACTIVATE				= 0x0584,
	B_WINDOW_SET_OWNER				= 0x0585,
	B_WINDOW_SET_TITLE				= 0x0587,
	B_WINDOW_IS_FRONT				= 0x0589,
	B_WINDOW_HIDE					= 0x058C,
	B_WINDOW_SHOW					= 0x058D,
	B_WINDOW_SET_SIZE_LIMITS		= 0x058E,
	B_WINDOW_IS_ACTIVE				= 0x058F,
//	B_WINDOW_MINIMIZE				= 0x0590,	// ????
	B_WINDOW_MINIMIZE				= 0x0591,
	B_WINDOW_SET_FLAGS				= 0x0593,
	B_WINDOW_SEND_BEHIND			= 0x0594,
	B_WINDOW_ADD_TO_SUBSET			= 0x0595,
	B_WINDOW_REMOVE_FROM_SUBSET		= 0x0596,
	B_WINDOW_SET_WINDOW_ALLIGNMENT	= 0x0597,
	B_WINDOW_GET_WINDOW_ALLIGNMENT	= 0x0598,
	B_WINDOW_SET_CURRENT_VIEW		= 0x059A,

// Drawing instructions

	B_MOVE_PEN_TO				= 0x0600,
	B_MOVE_PEN_BY				= 0x0601,
	B_STROKE_LINE				= 0x0602,
	B_STROKE_LINE_TO			= 0x0603,
	B_STROKE_RECT				= 0x0604,
	B_FILL_RECT					= 0x0605,
	B_STROKE_ARC_RECT			= 0x0606,
	B_FILL_ARC_RECT				= 0x0607,
	B_STROKE_ARC				= 0x0608,
	B_FILL_ARC					= 0x0609,
	B_STROKE_ROUND_RECT			= 0x060A,
	B_FILL_ROUND_RECT			= 0x060B,
	B_FILL_REGION				= 0x060D,
	B_STROKE_POLYGON			= 0x060E,
	B_FILL_POLYGON				= 0x060F,
	B_STROKE_BEZIER				= 0x0610,
	B_FILL_BEZIER				= 0x0611,
	B_STROKE_ELLIPSE_RECT		= 0x0612,
	B_FILL_ELLIPSE_RECT			= 0x0613,
	B_STROKE_ELLIPSE			= 0x0614,
	B_FILL_ELLIPSE				= 0x0615,
	B_DRAW_BITMAP				= 0x0616,
	B_STRETCH_BITMAP			= 0x0617,
	B_STRETCH_BITMAP_SRC		= 0x0618,
	B_DRAW_BITMAP_ASYNC			= 0x0619,
	B_STRETCH_BITMAP_ASYNC		= 0x061A,
	B_STRETCH_BITMAP_SRC_ASYNC	= 0x061B,
	B_DRAW_STRING				= 0x061C,	// ????
	B_COPY_BITS					= 0x061D,
	B_INVERT_RECT				= 0x061E,
	B_DRAW_LINES				= 0x061F,
	B_DRAW_PICTURE				= 0x0620,
	B_STROKE_SHAPE				= 0x0621,
	B_FILL_SHAPE				= 0x0622,

// Set instructions

	B_SET_DRAWING_MODE			= 0x0700,
	B_SCROLL_TO					= 0x0701,
	B_SET_PEN_SIZE				= 0x0702,
	B_SET_VIEW_COLOR			= 0x0703,
	B_SET_LINE_MODE				= 0x0704,
	B_SET_FONT					= 0x0705,
	B_SET_FORE_COLOR			= 0x0706,
	B_SET_BACK_COLOR			= 0x0707,
	B_CLEAR_CLIPPING_REGION		= 0x0708,
	B_SET_CLIPPING_REGION		= 0x0709,
	B_SET_SCALE					= 0x070A,
	B_SET_ORIGIN				= 0x070B,
	B_PUSH_STATE				= 0x070C,
	B_POP_STATE					= 0x070D,
	B_SET_PATTERN				= 0x070E,
	B_SET_VIEW_BITMAP			= 0x070F,	// ????
	B_FORCE_FONT_ANTIALIASING	= 0x0710,
	B_CLIP_TO_PICTURE			= 0x0711,
	B_SET_BLENDING_MODE			= 0x0712,

// Get instructions

	B_PEN_SIZE					= 0x0780,
	B_FORE_COLOR				= 0x0781,
	B_BACK_COLOR				= 0x0782,
	B_PEN_LOCATION				= 0x0783,
//	B_FRAME						= 0x0784,	// ????
	B_DRAWING_MODE				= 0x0785,
	B_CLIPPING_REGION			= 0x0786,
	B_LINE_CAP_MODE				= 0x0787,
	B_LINE_JOIN_MODE			= 0x0788,
	B_LINE_MITER_LIMIT			= 0x0789,
//	B_ORIGIN					= 0x078A,
	B_BLENDING_MODE				= 0x078B,
	B_FONT						= 0x078C,
	B_FRAME						= 0x078D,

// Misc instructions

	B_CONVERT_TO				= 0x0832,
	B_CONVERT_FROM				= 0x0833,
	B_INVALIDATE_RECT			= 0x0840,
	B_DISABLE_UPDATES			= 0x0841,
	B_ENABLES_UPDATES			= 0x0842,
	//????						= 0x0845,
	B_BEGIN_VIEW_TRANSACTION	= 0x0846,
	B_END_VIEW_TRANSACTION		= 0x0847,
	B_BEGIN_PICTURE				= 0x0850,
	B_APPEND_TO_PICTURE			= 0x0851,
	B_END_PICTURE				= 0x0852,
	B_GET_WORKSPACES			= 0x0860,
	B_SET_WORKSPACES			= 0x0861,

	// This is necessary because B_SHOW_CURSOR is defined in Accelerant.h
	B_BPIC_SHOW_CURSOR			= 0x0ECD,
	B_HIDE_CURSOR				= 0x0ECE,
	B_OBSCURE_CURSOR			= 0x0ECF,

	B_IS_CURSOR_HIDDEN			= 0x0EDA,

	B_SET_CURSOR				= 0x0F10,
};

#endif

