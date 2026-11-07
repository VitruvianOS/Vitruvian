/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_EMPTY_STATE_VIEW_H
#define WEB_MAIL_EMPTY_STATE_VIEW_H


#include <View.h>


// Placeholder shown instead of a MailView while no account exists:
// the three panes of the mail UI are laid out with their headers so
// the window reads as Mail, with the add account call to action in
// the message column.
class EmptyStateView : public BView {
public:
						EmptyStateView();
};


#endif // WEB_MAIL_EMPTY_STATE_VIEW_H
