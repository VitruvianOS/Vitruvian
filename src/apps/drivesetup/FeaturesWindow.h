/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */
#ifndef FEATURES_WINDOW_H
#define FEATURES_WINDOW_H


#include <Window.h>


class BColumnListView;


class FeaturesWindow : public BWindow {
public:
								FeaturesWindow(BWindow* window);
	virtual						~FeaturesWindow();

	virtual	bool				QuitRequested();

private:
			void				_Populate();

			BColumnListView*	fListView;
};


#endif // FEATURES_WINDOW_H
