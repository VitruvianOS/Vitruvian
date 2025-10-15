/*
 * Copyright 2019, Dario Casalinuovo
 * Distributed under the terms of the LGPL License.
 */

#include "LibInputEventStream.h"

#include <Autolock.h>
#include <Screen.h>

#include <errno.h>
#include <sys/epoll.h>

#include <algorithm>


static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}


static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}


const static struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};


LibInputEventStream::LibInputEventStream(uint32 width, uint32 height)
	:
	fEventList(10, true),
	fEventListLocker("remote event list"),
	fEventNotification(-1),
	fWaitingOnEvent(false),
	fLatestMouseMovedEvent(NULL),
	fMousePosition(0, 0),
	fMouseButtons(0),
	fModifiers(0),
	fRunning(true),
	fWidth(width),
	fHeight(height)
{
	fEventNotification = create_sem(0, "remote event notification");

	fUDevHandle = udev_new();
	fInputHandle = libinput_udev_create_context(&interface, NULL,
		fUDevHandle);
	libinput_udev_assign_seat(fInputHandle, "seat0");

	thread_id pollThread
		= spawn_thread(_PollEventsThread, "semper ad maiora",
			B_NORMAL_PRIORITY, (void*)this);

	resume_thread(pollThread);

	BScreen screen;
	screen.Frame().PrintToStream();
}


LibInputEventStream::~LibInputEventStream()
{
	fRunning = false;

	delete_sem(fEventNotification);

	libinput_unref(fInputHandle);
	udev_unref(fUDevHandle);
}


void
LibInputEventStream::UpdateScreenBounds(BRect bounds)
{
}


bool
LibInputEventStream::GetNextEvent(BMessage** _event)
{
	BAutolock lock(fEventListLocker);
	while (fEventList.CountItems() == 0) {
		fWaitingOnEvent = true;
		lock.Unlock();

		status_t result;
		do {
			result = acquire_sem(fEventNotification);
		} while (result == B_INTERRUPTED);

		lock.Lock();
		if (!lock.IsLocked())
			return false;
	}

	*_event = fEventList.RemoveItemAt(0);
	return true;
}


status_t
LibInputEventStream::InsertEvent(BMessage* event)
{
	BAutolock lock(fEventListLocker);
	if (!lock.IsLocked() || !fEventList.AddItem(event))
		return B_ERROR;

	if (event->what == B_MOUSE_MOVED)
		fLatestMouseMovedEvent = event;

	return B_OK;
}


BMessage*
LibInputEventStream::PeekLatestMouseMoved()
{
	return fLatestMouseMovedEvent;
}


void
LibInputEventStream::_PollEventsThread(void* cookie)
{
	LibInputEventStream* fOwner = (LibInputEventStream*)cookie;
	fOwner->_PollEvents();

}


void
LibInputEventStream::_PollEvents()
{
	int poll = epoll_create1(0);
	if (poll < 0)
		return;

	epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = libinput_get_fd(fInputHandle);

	if (epoll_ctl(poll, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
		close(poll);
		return;
	}

	while (fRunning) {
		epoll_wait(poll, &event, 1, 200);

		if (libinput_dispatch(fInputHandle) < 0) {
			close(poll);
			return;
		}

		libinput_event* inputEvent;
		while ((inputEvent
			= libinput_get_event(fInputHandle)) != NULL) {
			_ScheduleEvent(inputEvent);
			libinput_event_destroy(inputEvent);
		}
	}

	if (close(poll) < 0)
		return;
}


void
LibInputEventStream::_ScheduleEvent(libinput_event* ev)
{
	libinput_event_type type = libinput_event_get_type(ev);
	libinput_device* dev = libinput_event_get_device(ev);
	uint32 what = 0;

	BMessage* event = new BMessage();
	if (event == NULL)
		return;

	switch (type)
	{
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			break;

		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		{
			libinput_event_pointer* e
				= libinput_event_get_pointer_event(ev);

			what = B_MOUSE_MOVED;

			double dx = libinput_event_pointer_get_absolute_x_transformed(e, fWidth);
			double dy = libinput_event_pointer_get_absolute_y_transformed(e, fHeight);
			fMousePosition.x = static_cast<int>(dx);
			fMousePosition.y = static_cast<int>(dy);
			event->AddPoint("where", fMousePosition);

			fLatestMouseMovedEvent = event;
			break;
		}

		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		{
			libinput_event_pointer* e
				= libinput_event_get_pointer_event(ev);

			if (type == LIBINPUT_EVENT_POINTER_MOTION)
				what = B_MOUSE_MOVED;
			else if (libinput_event_pointer_get_button_state(e)
					== LIBINPUT_BUTTON_STATE_PRESSED) {
				what = B_MOUSE_DOWN;
			} else
				what = B_MOUSE_UP;

			fMouseButtons = libinput_event_pointer_get_button(e);

			double dx = libinput_event_pointer_get_dx(e);
			double dy = libinput_event_pointer_get_dy(e);

			fMousePosition.x += static_cast<int>(dx);
			fMousePosition.y += static_cast<int>(dy);

			fMousePosition.x = std::min((float)fWidth, fMousePosition.x);
			fMousePosition.x = std::max(0.0f, fMousePosition.x);

			fMousePosition.y = std::min((float)fHeight, fMousePosition.y);
			fMousePosition.y = std::max(0.0f, fMousePosition.y);

			event->AddPoint("where", fMousePosition);
			if (what == B_MOUSE_MOVED || what == B_MOUSE_DOWN)
				event->AddInt32("buttons", fMouseButtons);

			//event->AddInt32("modifiers", fModifiers);

			//if (what == B_MOUSE_DOWN)
			//	event->AddInt32("clicks", 0);

			fLatestMouseMovedEvent = event;
			break;
		}

		//case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		//break;

		case LIBINPUT_EVENT_POINTER_AXIS:
		{
			libinput_event_pointer* e
				= libinput_event_get_pointer_event(ev);

			double value = 0;
			if (libinput_event_pointer_has_axis(e,
					LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
				value = libinput_event_pointer_get_axis_value(
					e, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
				event->AddFloat("be:wheel_delta_y", value);
			}
			if (libinput_event_pointer_has_axis(e,
					LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
				value = libinput_event_pointer_get_axis_value(
					e, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
				event->AddFloat("be:wheel_delta_x", value);
			}

			what = B_MOUSE_WHEEL_CHANGED;
			break;
		}

		case LIBINPUT_EVENT_KEYBOARD_KEY:
		{
			/*libinput_event_keyboard* e
				= libinput_event_get_keyboard_event(ev);
			const uint32_t keycode
				= libinput_event_keyboard_get_key(e) + 8;

			if (libinput_event_keyboard_get_key_state(e)
					== LIBINPUT_KEY_STATE_PRESSED) {
				what = B_KEY_DOWN;
			} else
				what = B_KEY_UP;

			event->AddInt32("key", keycode);*/
			break;
		}

		case LIBINPUT_EVENT_TOUCH_DOWN:
		case LIBINPUT_EVENT_TOUCH_MOTION:
		case LIBINPUT_EVENT_TOUCH_UP:
		case LIBINPUT_EVENT_TOUCH_CANCEL:
		case LIBINPUT_EVENT_TOUCH_FRAME:
		default:
			break;
	}

	if (what == 0)
		return;

	event->what = what;

	event->AddInt64("when", system_time());

	BAutolock lock(fEventListLocker);
	fEventList.AddItem(event);
	if (fWaitingOnEvent) {
		fWaitingOnEvent = false;
		lock.Unlock();
		release_sem(fEventNotification);
	}
}
