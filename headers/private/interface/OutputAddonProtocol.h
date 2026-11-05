/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OUTPUT_ADDON_PROTOCOL_H
#define _OUTPUT_ADDON_PROTOCOL_H


// Wire format shared by app_server, the output add-on client library and
// the host checks. Pure C structs so the transport core builds with a
// plain compiler; no interface-kit objects.


#include <SupportDefs.h>


#define OUTPUT_ADDON_MAGIC	0x4f414642	// 'OAFB'

enum {
	kOutputAddonProtocolVersion	= 1,
};


// Transport lanes one protocol offers; add-ons pick at attach time.
enum {
	kOutputAddonTransportSharedDamage	= 1,
		// damage rects plus a shared RGBA32 area, double buffered
	kOutputAddonTransportPicture		= 2,
		// BPicture stream payload per frame; reserved, see REPORT
	kOutputAddonTransportVideo			= 3,
		// encoded video; reserved, see REPORT
};


// Update port codes: app_server to add-on.
enum {
	kOutputAddonCodeFrame			= 1,
	kOutputAddonCodeScreenChanged	= 2,
	kOutputAddonCodeOutputRemoved	= 3,
};


// Control channel BMessage what codes: add-on to app_server, answered
// on the add-on's reply port. The well-known port name is below.
#define OUTPUT_ADDON_PORT_NAME	"v-os:output_addon"

enum {
	kMsgOutputAddonRegister			= 'oaRG',
	kMsgOutputAddonQuit				= 'oaQT',
	kMsgOutputAddonCreateVirtual	= 'oaCV',
	kMsgOutputAddonDeleteVirtual	= 'oaDV',
	kMsgOutputAddonAttach			= 'oaAT',
	kMsgOutputAddonDetach			= 'oaDT',
	kMsgOutputAddonInjectInput		= 'oaIN',
	kMsgOutputAddonLaunch			= 'oaLO',
};


enum {
	kOutputAddonMaxRects			= 256,
	kOutputAddonMaxWidth			= 16384,
	kOutputAddonMaxHeight			= 16384,
};


// Pixel rect on the wire: left/top inclusive, right/bottom exclusive,
// matching app_server's IntRect conversion from clipping_rect.
struct output_addon_rect {
	int32	left;
	int32	top;
	int32	right;
	int32	bottom;
};


// Shared framebuffer area layout: this header, then buffer_count pixel
// slots of bytes_per_row * height each. front and acked pack a frame
// generation with the slot index holding it; the server must not reuse
// a slot until the client has acked the generation published in it.
struct output_addon_fb_header {
	uint32	magic;
	int32	width;
	int32	height;
	int32	bytes_per_row;
	int32	buffer_count;
	int32	transport;
	uint32	front;
	uint32	acked;
	uint32	reserved[8];
};


// Payload of one kOutputAddonCodeFrame port write: this header followed
// by rect_count output_addon_rects. Pixels live in the shared area.
struct output_addon_frame {
	int32	screen;
	uint32	generation;
	int32	rect_count;
};


#ifdef __cplusplus
extern "C" {
#endif

void	output_addon_rect_set(output_addon_rect* rect, int32 left,
				int32 top, int32 right, int32 bottom);
bool	output_addon_rect_empty(const output_addon_rect* rect);
void	output_addon_rect_union(output_addon_rect* ioRect,
				const output_addon_rect* add);
bool	output_addon_rect_intersect(const output_addon_rect* a,
				const output_addon_rect* b, output_addon_rect* out);

uint32	output_addon_pack(uint32 generation, uint32 index);
void	output_addon_unpack(uint32 front, uint32* generation,
				uint32* index);
void	output_addon_publish(volatile uint32* front, uint32 generation,
				uint32 index);

int32	output_addon_fb_header_size();
int32	output_addon_fb_area_size(int32 bytesPerRow, int32 height,
				int32 bufferCount);
void*	output_addon_fb_buffer(void* base, int32 bytesPerRow, int32 height,
				int32 index);

int32	output_addon_frame_encode(const output_addon_frame* frame,
				const output_addon_rect* rects, void* outBuffer,
				int32 outSize);
int32	output_addon_frame_decode(const void* buffer, int32 size,
				output_addon_frame* frame,
				const output_addon_rect** rects);

void	output_addon_copy_rect(uint8* dstBase, int32 dstStride,
				const uint8* srcBase, int32 srcStride,
				const output_addon_rect* rects, int32 rectCount);

#ifdef __cplusplus
}
#endif


#endif	// _OUTPUT_ADDON_PROTOCOL_H
