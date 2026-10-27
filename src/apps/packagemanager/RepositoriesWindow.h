/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef REPOSITORIES_WINDOW_H
#define REPOSITORIES_WINDOW_H

#include <Window.h>


class BColumnListView;


// Read-only list of the configured apt sources (deb822 and one-line).
// TODO: add, remove and enable/disable once vos-apt-helper has a verb
// for writing sources.
class RepositoriesWindow : public BWindow {
	typedef BWindow Inherited;
public:
								RepositoriesWindow();

	virtual	bool				QuitRequested();

private:
			void				_Load();
			void				_LoadDeb822(const char* path);
			void				_LoadOneLine(const char* path);
			void				_AddRow(const char* file, const char* type,
									const char* uris, const char* suites,
									const char* components, bool enabled);

			BColumnListView*	fListView;
};


#endif
