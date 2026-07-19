/*
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef DEFAULT_MEDIA_THEME_H
#define DEFAULT_MEDIA_THEME_H


#include <media2/MediaTheme.h>


class BParameterGroup;


namespace BPrivate {

class DefaultMediaTheme : public BMediaTheme {
public:
		DefaultMediaTheme();

		virtual	BControl* MakeControlFor(BParameter* parameter);

		static BControl* MakeViewFor(BParameter* parameter);

protected:
		virtual	BView* MakeViewFor(BParameterWeb* web, const BRect* hintRect = NULL);

private:
		BView* MakeViewFor(BParameterGroup& group);
		BView* MakeSelfHostingViewFor(BParameter& parameter);
};

}	// namespace BPrivate

#endif	/* DEFAULT_MEDIA_THEME_H */
