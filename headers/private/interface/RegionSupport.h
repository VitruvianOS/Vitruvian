// This file is distributed under the OpenBeOS license

#ifndef __REGION_SUPPORT_H
#define __REGION_SUPPORT_H

#include <Region.h>

class BRegion::Support 
{
public:
	static void ZeroRegion(BRegion *a_region);
	static void ClearRegion(BRegion *a_region);
	static void CopyRegion(BRegion *src_region, BRegion *dst_region);
	static void AndRegion(BRegion *first, BRegion *second, BRegion *dest);
	static void OrRegion(BRegion *first, BRegion *second, BRegion *dest);
	static void SubRegion(BRegion *first, BRegion *second, BRegion *dest);

private:
	static void CleanupRegion(BRegion *region_in);
	static void CleanupRegionVertical(BRegion *region_in);
	static void CleanupRegionHorizontal(BRegion *region_in);
	
	static void SortRects(clipping_rect *rects, int32 count);
	static void SortTrans(int32 *lptr1, int32 *lptr2, int32 count);	
	
	static void CopyRegionMore(BRegion*, BRegion*, int32);
	
	static void AndRegionComplex(BRegion*, BRegion*, BRegion*);
	static void AndRegion1ToN(BRegion*, BRegion*, BRegion*);

	static void AppendRegion(BRegion*, BRegion*, BRegion*);
	
	static void OrRegionComplex(BRegion*, BRegion*, BRegion*);
	static void OrRegion1ToN(BRegion*, BRegion*, BRegion*);
	static void OrRegionNoX(BRegion*, BRegion*, BRegion*);
	static void ROr(int32, int32, BRegion*, BRegion*, BRegion*, int32*, int32 *);

	static void SubRegionComplex(BRegion*, BRegion*, BRegion*);
	static void RSub(int32 , int32, BRegion*, BRegion*, BRegion*, int32*, int32*);
};

#endif // __REGION_SUPPORT_H
