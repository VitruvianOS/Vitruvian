/*
 * Copyright 2000, Georges-Edouard Berenger. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AUTO_ICON_H_
#define _AUTO_ICON_H_


#include <SupportDefs.h>

class BBitmap;


// Disambiguates the resource constructor from the signature one, as both
// take a name.
enum resource_icon_t {
	kResourceIcon
};


class AutoIcon {
	public:
		AutoIcon(const char* signature)
			:
			fSignature(signature),
			fbits(0),
			fResourceName(0),
			fBitmap(0)
		{
		}

		AutoIcon(const uchar* bits)
			:
			fSignature(0),
			fbits(bits),
			fResourceName(0),
			fBitmap(0)
		{
		}

		// Vector icon from this add-on's own resources.
		AutoIcon(const char* resourceName, resource_icon_t)
			:
			fSignature(0),
			fbits(0),
			fResourceName(resourceName),
			fBitmap(0)
		{
		}

		~AutoIcon();

	  	operator BBitmap*()
	  	{
	  		return Bitmap();
	  	}

		BBitmap* Bitmap();

	private:
		const char*		fSignature;
		const uchar*	fbits;
		const char*		fResourceName;
		BBitmap*		fBitmap;
};

#endif // _AUTO_ICON_H_
