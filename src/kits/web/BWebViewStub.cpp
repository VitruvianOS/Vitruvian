/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the MIT License.
 *
 * BWebView stubs for compile-check builds (WEB_COMPILE_ONLY).
 * The real implementation lives in the web AddOn which requires
 * WPE/WebKit; this file lets apps that link against "web" resolve
 * BWebView symbols when the full stack is unavailable.
 */

#include <web/WebView.h>

#include <String.h>


class BWebView::Impl {};


BWebView::BWebView(BRect frame, const char* name,
	uint32 resizingMode, uint32 flags)
	:
	BView(frame, name, resizingMode, flags),
	fImpl(nullptr)
{
}


BWebView::~BWebView()
{
	delete fImpl;
}


void
BWebView::LoadURL(const char* /*url*/)
{
}


void
BWebView::LoadHTML(const char* /*html*/, const char* /*baseURL*/)
{
}


void
BWebView::Reload()
{
}


void
BWebView::Stop()
{
}


void
BWebView::GoBack()
{
}


void
BWebView::GoForward()
{
}


bool
BWebView::CanGoBack() const
{
	return false;
}


bool
BWebView::CanGoForward() const
{
	return false;
}


void
BWebView::SetTarget(const BMessenger& /*target*/)
{
}


const char*
BWebView::CurrentURL() const
{
	return "";
}


const char*
BWebView::Title() const
{
	return "";
}


float
BWebView::LoadProgress() const
{
	return 0.0f;
}


bool
BWebView::IsLoading() const
{
	return false;
}


void
BWebView::EvaluateJS(const char* /*script*/)
{
}


void
BWebView::AttachedToWindow()
{
	BView::AttachedToWindow();
}


void
BWebView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BWebView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
}


void
BWebView::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}
