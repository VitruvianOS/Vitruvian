#include <LinuxBuildCompatibility.h>

#include <ByteOrder.h>

#if 0
uint16
__swap_int16(uint16 value)
{
	return (value >> 8) | (value << 8);
}

uint32
__swap_int32(uint32 value)
{
	return (value >> 24) | ((value & 0xff0000) >> 8) | ((value & 0xff00) << 8)
		| (value << 24);
}

uint64
__swap_int64(uint64 value)
{
	return uint64(__swap_int32(uint32(value >> 32)))
		| (uint64(__swap_int32(uint32(value))) << 32);
}
#else
double __swap_double(double arg)
{
	// FIXME: unimplemented
	return arg;
}

float  __swap_float(float arg)
{
	// FIXME: unimplemented
	return arg;
}

uint64 __swap_int64(uint64 uarg)
{
	unsigned char	b1, b2, b3, b4, b5, b6, b7, b8;

	// Separate out each of the 8-bytes of this uint64
	b1 = (uarg >> 56) & 0xff;
	b2 = (uarg >> 48) & 0xff;
	b3 = (uarg >> 40) & 0xff;
	b4 = (uarg >> 32) & 0xff;
	b5 = (uarg >> 24) & 0xff;
	b6 = (uarg >> 16) & 0xff;
	b7 = (uarg >>  8) & 0xff;
	b8 =  uarg        & 0xff;

	// Return them reassembled in reverse order
	return ((uint64)b8 << 56) | ((uint64)b7 << 48) |
		   ((uint64)b6 << 40) | ((uint64)b5 << 32) |
		   ((uint64)b4 << 24) | ((uint64)b3 << 16) |
		   ((uint64)b2 << 8)  |  (uint64)b1;
}

uint32 __swap_int32(uint32 uarg)
{
	unsigned char	b1, b2, b3, b4;

	// Separate out each of the 4-bytes of this uint32
	b1 = (uarg >> 24) & 0xff;
	b2 = (uarg >> 16) & 0xff;
	b3 = (uarg >>  8) & 0xff;
	b4 =  uarg        & 0xff;

	// Return them reassembled in reverse order
	return ((uint32)b4 << 24) | ((uint32)b3 << 16) | ((uint32)b2 << 8) | (uint32)b1;
}

uint16 __swap_int16(uint16 uarg)
{
	unsigned char	b1, b2;

	// Separate out the 2-bytes of this uint16
	b1 = (uarg >>  8) & 0xff;
	b2 =  uarg        & 0xff;

	// Return them reassembled in reverse order
	return (b2 << 8) | b1;
}
#endif
