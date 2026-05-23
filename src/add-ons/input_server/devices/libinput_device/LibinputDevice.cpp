#include "LibinputDevice.h"

#include <Debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <Autolock.h>
#include <Message.h>
#include <String.h>
#include <View.h>

#include <libinput.h>
#include <libudev.h>
#include "../LinuxEvdevShim.h"


static const int32 kPollTimeout = 100;


LibinputDeviceObject::LibinputDeviceObject(struct libinput_device* dev)
	:
	fDevice(dev),
	fButtons(0),
	fLastPosition(640, 400)
{
	libinput_device_ref(fDevice);
	fName = libinput_device_get_name(dev);
	if (fName.Length() == 0)
		fName = "Unknown Touchpad/Tablet";
}


LibinputDeviceObject::~LibinputDeviceObject()
{
	if (fDevice != NULL)
		libinput_device_unref(fDevice);
}


void
LibinputDeviceObject::UpdateButtons(uint32 buttons)
{
	fButtons = buttons;
}


LibinputDevice::LibinputDevice()
	:
	BInputServerDevice(),
	fInitStatus(B_OK),
	fLibinput(NULL),
	fUdev(NULL),
	fEventThread(-1),
	fRunning(false),
	fScreenW(1280),
	fScreenH(800),
	fSuspended(false),
	fStarted(false),
	fScrollX(0),
	fScrollY(0),
	fTouchSlot1Active(false),
	fTouchSlot1LastY(0),
	fTouchSlot1LastX(0),
	fTouchScrollAccX(0),
	fTouchScrollAccY(0),
	fLastClickTime(0),
	fClickCount(0),
	fLastClickButton(0)
{
	fSyntheticRef.name = (char*)"libinput";
	fSyntheticRef.type = B_POINTING_DEVICE;
	fSyntheticRef.cookie = this;

	input_device_ref* refs[2] = { &fSyntheticRef, NULL };
	RegisterDevices(refs);
}


LibinputDevice::~LibinputDevice()
{
	if (fLibinput != NULL)
		libinput_unref(fLibinput);
	if (fUdev != NULL)
		udev_unref(fUdev);
}


status_t
LibinputDevice::InitCheck()
{
	return fInitStatus;
}


static int
_open_restricted(const char* path, int flags, void* /*data*/)
{
	return open(path, flags);
}


static void
_close_restricted(int fd, void* /*data*/)
{
	close(fd);
}


static const struct libinput_interface kLibinputInterface = {
	.open_restricted  = _open_restricted,
	.close_restricted = _close_restricted,
};


status_t
LibinputDevice::Start(const char* /*name*/, void* /*cookie*/)
{
	if (fStarted)
		return B_OK;
	fStarted = true;

	if (fLibinput != NULL)
		return B_OK;

	fUdev = udev_new();
	if (fUdev == NULL) {
		debug_printf("libinput_device: udev_new failed\n");
		return B_ERROR;
	}

	fLibinput = libinput_udev_create_context(&kLibinputInterface, NULL, fUdev);
	if (fLibinput == NULL) {
		debug_printf("libinput_device: libinput_udev_create_context failed\n");
		udev_unref(fUdev);
		fUdev = NULL;
		return B_ERROR;
	}

	const char* seat = getenv("XDG_SEAT");
	if (seat == NULL)
		seat = "seat0";

	if (libinput_udev_assign_seat(fLibinput, seat) != 0) {
		debug_printf("libinput_device: failed to assign seat %s\n", seat);
		libinput_unref(fLibinput);
		fLibinput = NULL;
		udev_unref(fUdev);
		fUdev = NULL;
		return B_ERROR;
	}

	fRunning = true;
	fEventThread = spawn_thread(_EventThreadEntry, "libinput_event",
		B_REAL_TIME_DISPLAY_PRIORITY, this);
	if (fEventThread < 0) {
		fRunning = false;
		libinput_unref(fLibinput);
		fLibinput = NULL;
		udev_unref(fUdev);
		fUdev = NULL;
		return fEventThread;
	}

	resume_thread(fEventThread);

	fInitStatus = B_OK;
	return B_OK;
}


status_t
LibinputDevice::Stop(const char* /*name*/, void* /*cookie*/)
{
	fRunning = false;
	if (fEventThread >= 0) {
		status_t dummy;
		wait_for_thread(fEventThread, &dummy);
		fEventThread = -1;
	}

	fDeviceListLock.Lock();
	for (int32 i = 0; i < fDevices.CountItems(); i++)
		delete fDevices.ItemAt(i);
	fDevices.MakeEmpty();
	fDeviceListLock.Unlock();

	if (fLibinput != NULL) {
		libinput_unref(fLibinput);
		fLibinput = NULL;
	}
	if (fUdev != NULL) {
		udev_unref(fUdev);
		fUdev = NULL;
	}

	fInitStatus = B_NO_INIT;
	return B_OK;
}


status_t
LibinputDevice::Control(const char* /*name*/, void* /*cookie*/,
	uint32 command, BMessage* message)
{
	return B_OK;
}


int32
LibinputDevice::_EventThreadEntry(void* data)
{
	return static_cast<LibinputDevice*>(data)->_EventThread();
}


int32
LibinputDevice::_EventThread()
{
	int fd = libinput_get_fd(fLibinput);

	libinput_dispatch(fLibinput);
	while (fRunning) {
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		int ret = poll(&pfd, 1, kPollTimeout);
		if (ret > 0) {
			libinput_dispatch(fLibinput);

			struct libinput_event* event;
			while ((event = libinput_get_event(fLibinput)) != NULL) {
				_HandleEvent(event);
				libinput_event_destroy(event);
			}
		}
	}

	return 0;
}


void
LibinputDevice::_HandleEvent(struct libinput_event* event)
{
	enum libinput_event_type type = libinput_event_get_type(event);

	switch (type) {
		case LIBINPUT_EVENT_DEVICE_ADDED:
			_HandleDeviceAdded(libinput_event_get_device(event));
			break;
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			_HandleDeviceRemoved(libinput_event_get_device(event));
			break;

		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		{
			if (fSuspended) break;
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);
			double x = libinput_event_pointer_get_absolute_x_transformed(
				pev, fScreenW);
			double y = libinput_event_pointer_get_absolute_y_transformed(
				pev, fScreenH);

			struct libinput_device* dev = libinput_event_get_device(event);
			LibinputDeviceObject* obj = _FindObject(dev);
			if (obj == NULL) break;

			obj->SetLastPosition(BPoint(x, y));

			BMessage* msg = new BMessage(B_MOUSE_MOVED);
			msg->AddPoint("where", obj->LastPosition());
			msg->AddInt32("buttons", (int32)obj->Buttons());
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);
			EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_BUTTON:
		{
			if (fSuspended) break;
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);
			uint32_t button = libinput_event_pointer_get_button(pev);
			enum libinput_button_state state =
				libinput_event_pointer_get_button_state(pev);

			struct libinput_device* dev = libinput_event_get_device(event);
			LibinputDeviceObject* obj = _FindObject(dev);
			if (obj == NULL) break;

			uint32 beButton = 0;
			if (button == BTN_LEFT)         beButton = 0x1;
			else if (button == BTN_RIGHT)   beButton = 0x2;
			else if (button == BTN_MIDDLE)  beButton = 0x4;

			uint32 oldButtons = obj->Buttons();
			uint32 newButtons = oldButtons;
			if (state == LIBINPUT_BUTTON_STATE_PRESSED)
				newButtons |= beButton;
			else
				newButtons &= ~beButton;
			obj->UpdateButtons(newButtons);

			uint32 what = (state == LIBINPUT_BUTTON_STATE_PRESSED)
				? B_MOUSE_DOWN : B_MOUSE_UP;

			BMessage* msg = new BMessage(what);
			msg->AddPoint("where", obj->LastPosition());
			msg->AddInt32("buttons", (int32)newButtons);
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_MOUSE_POINTING_DEVICE);
			if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
				bigtime_t now = system_time();
				if (beButton == fLastClickButton
					&& (now - fLastClickTime) < 300000LL)
					fClickCount++;
				else
					fClickCount = 1;
				fLastClickTime = now;
				fLastClickButton = beButton;
				msg->AddInt32("clicks", fClickCount);
				msg->AddInt32("be:button", (int32)beButton);
			}
			EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
		{
			if (fSuspended) break;
			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);

			double rawDx = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
			double rawDy = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

			fScrollX += rawDx;
			fScrollY += rawDy;

			int32 stepsX = 0, stepsY = 0;
			float stepSize = 1.0f;

			if (fScrollX >= stepSize || fScrollX <= -stepSize) {
				stepsX = (int32)(fScrollX / stepSize);
				fScrollX -= stepsX * stepSize;
			}
			if (fScrollY >= stepSize || fScrollY <= -stepSize) {
				stepsY = (int32)(fScrollY / stepSize);
				fScrollY -= stepsY * stepSize;
			}

			if (stepsX == 0 && stepsY == 0)
				break;

			struct libinput_device* dev = libinput_event_get_device(event);
			LibinputDeviceObject* obj = _FindObject(dev);
			if (obj == NULL) break;

			BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
			msg->AddInt64("when", system_time());
			msg->AddFloat("be:wheel_delta_x", (float)stepsX);
			msg->AddFloat("be:wheel_delta_y", (float)stepsY);
			msg->AddPoint("where", obj->LastPosition());
			msg->AddInt32("buttons", 0);
			msg->AddInt32("modifiers", 0);
			EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
		{
			if (fSuspended) break;
			struct libinput_device* scrollDev = libinput_event_get_device(event);
			if (libinput_device_has_capability(scrollDev,
					LIBINPUT_DEVICE_CAP_POINTER)
				&& !libinput_device_has_capability(scrollDev,
					LIBINPUT_DEVICE_CAP_TOUCH)
				&& !libinput_device_has_capability(scrollDev,
					LIBINPUT_DEVICE_CAP_TABLET_TOOL))
				break;

			struct libinput_event_pointer* pev =
				libinput_event_get_pointer_event(event);

			double dx = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
			double dy = libinput_event_pointer_get_scroll_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

			fScrollX += dx;
			fScrollY += dy;

			int32 stepsX = 0, stepsY = 0;
			float stepSize = 1.0f;

			if (fScrollX >= stepSize || fScrollX <= -stepSize) {
				stepsX = (int32)(fScrollX / stepSize);
				fScrollX -= stepsX * stepSize;
			}
			if (fScrollY >= stepSize || fScrollY <= -stepSize) {
				stepsY = (int32)(fScrollY / stepSize);
				fScrollY -= stepsY * stepSize;
			}

			if (stepsX == 0 && stepsY == 0)
				break;

			LibinputDeviceObject* obj = _FindObject(scrollDev);
			if (obj == NULL) break;

			BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
			msg->AddInt64("when", system_time());
			msg->AddFloat("be:wheel_delta_x", (float)stepsX);
			msg->AddFloat("be:wheel_delta_y", (float)-stepsY);
			msg->AddPoint("where", obj->LastPosition());
			msg->AddInt32("buttons", 0);
			msg->AddInt32("modifiers", 0);
			EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_TOUCH_DOWN:
		case LIBINPUT_EVENT_TOUCH_UP:
		case LIBINPUT_EVENT_TOUCH_MOTION:
		{
			if (fSuspended) break;
			struct libinput_event_touch* tev =
				libinput_event_get_touch_event(event);
			int32_t slot = libinput_event_touch_get_slot(tev);

			// Slot 1 = second finger: track position for two-finger scroll
			if (slot == 1) {
				if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
					float sx = (float)libinput_event_touch_get_x_transformed(
						tev, fScreenW);
					float sy = (float)libinput_event_touch_get_y_transformed(
						tev, fScreenH);
					fTouchSlot1Active = true;
					fTouchSlot1LastX  = sx;
					fTouchSlot1LastY  = sy;
					fTouchScrollAccX  = 0;
					fTouchScrollAccY  = 0;
				} else if (type == LIBINPUT_EVENT_TOUCH_UP) {
					fTouchSlot1Active = false;
					fTouchScrollAccX  = 0;
					fTouchScrollAccY  = 0;
				} else if (type == LIBINPUT_EVENT_TOUCH_MOTION
					&& fTouchSlot1Active) {
					float sx = (float)libinput_event_touch_get_x_transformed(
						tev, fScreenW);
					float sy = (float)libinput_event_touch_get_y_transformed(
						tev, fScreenH);
					float dx = sx - fTouchSlot1LastX;
					float dy = sy - fTouchSlot1LastY;
					fTouchSlot1LastX = sx;
					fTouchSlot1LastY = sy;

					fTouchScrollAccX += dx;
					fTouchScrollAccY += dy;

					const float kScrollStep = 30.0f; // px per scroll step
					int32 stepsX = 0, stepsY = 0;
					if (fTouchScrollAccX >= kScrollStep
						|| fTouchScrollAccX <= -kScrollStep) {
						stepsX = (int32)(fTouchScrollAccX / kScrollStep);
						fTouchScrollAccX -= stepsX * kScrollStep;
					}
					if (fTouchScrollAccY >= kScrollStep
						|| fTouchScrollAccY <= -kScrollStep) {
						stepsY = (int32)(fTouchScrollAccY / kScrollStep);
						fTouchScrollAccY -= stepsY * kScrollStep;
					}

					if (stepsX != 0 || stepsY != 0) {
						struct libinput_device* dev =
							libinput_event_get_device(event);
						LibinputDeviceObject* obj = _FindObject(dev);
						BPoint where = (obj != NULL)
							? obj->LastPosition()
							: BPoint(fScreenW / 2, fScreenH / 2);

						BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
						msg->AddInt64("when", system_time());
						msg->AddFloat("be:wheel_delta_x", (float)stepsX);
						msg->AddFloat("be:wheel_delta_y", (float)stepsY);
						msg->AddPoint("where", where);
						msg->AddInt32("buttons", 0);
						msg->AddInt32("modifiers", 0);
						EnqueueMessage(msg);
					}
				}
				break;
			}

			// Slot 0 = first finger: pointer events
			if (slot != 0)
				break;

			struct libinput_device* dev = libinput_event_get_device(event);
			LibinputDeviceObject* obj = _FindObject(dev);
			if (obj == NULL) break;

			if (type == LIBINPUT_EVENT_TOUCH_DOWN
				|| type == LIBINPUT_EVENT_TOUCH_MOTION) {
				// Skip pointer move while two-finger scroll is active
				if (fTouchSlot1Active && type == LIBINPUT_EVENT_TOUCH_MOTION)
					break;

				float sx = (float)libinput_event_touch_get_x_transformed(
					tev, fScreenW);
				float sy = (float)libinput_event_touch_get_y_transformed(
					tev, fScreenH);
				obj->SetLastPosition(BPoint(sx, sy));

				uint32 what = (type == LIBINPUT_EVENT_TOUCH_DOWN)
					? B_MOUSE_DOWN : B_MOUSE_MOVED;
				uint32 btns = obj->Buttons();
				if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
					btns |= 0x1;
					obj->UpdateButtons(btns);
				}

				BMessage* msg = new BMessage(what);
				msg->AddPoint("where", obj->LastPosition());
				msg->AddInt32("buttons", (int32)btns);
				msg->AddInt32("modifiers", 0);
				msg->AddInt64("when", system_time());
				msg->AddInt32("be:device_subtype", B_TOUCHPAD_POINTING_DEVICE);
				if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
					msg->AddInt32("clicks", 1);
					msg->AddInt32("be:button", 1);
				}
				EnqueueMessage(msg);
			} else {
				uint32 btns = obj->Buttons() & ~0x1;
				obj->UpdateButtons(btns);

				BMessage* msg = new BMessage(B_MOUSE_UP);
				msg->AddPoint("where", obj->LastPosition());
				msg->AddInt32("buttons", (int32)btns);
				msg->AddInt32("modifiers", 0);
				msg->AddInt64("when", system_time());
				msg->AddInt32("be:device_subtype", B_TOUCHPAD_POINTING_DEVICE);
				msg->AddInt32("be:button", 1);
				EnqueueMessage(msg);
			}
			break;
		}

		case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		{
			if (fSuspended) break;
			struct libinput_event_tablet_tool* tev =
				libinput_event_get_tablet_tool_event(event);
			double x = libinput_event_tablet_tool_get_x(tev);
			double y = libinput_event_tablet_tool_get_y(tev);

			struct libinput_device* dev = libinput_event_get_device(event);
			LibinputDeviceObject* obj = _FindObject(dev);
			if (obj == NULL) break;

			float sx = (float)(x * fScreenW);
			float sy = (float)(y * fScreenH);
			obj->SetLastPosition(BPoint(sx, sy));

			BMessage* msg = new BMessage(B_MOUSE_MOVED);
			msg->AddPoint("where", obj->LastPosition());
			msg->AddInt32("buttons", (int32)obj->Buttons());
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_TABLET_POINTING_DEVICE);
			EnqueueMessage(msg);
			break;
		}

		case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		{
			if (fSuspended) break;
			struct libinput_event_tablet_tool* tev =
				libinput_event_get_tablet_tool_event(event);
			uint32_t button = libinput_event_tablet_tool_get_button(tev);
			enum libinput_button_state state =
				libinput_event_tablet_tool_get_button_state(tev);

			struct libinput_device* dev = libinput_event_get_device(event);
			LibinputDeviceObject* obj = _FindObject(dev);
			if (obj == NULL) break;

			uint32 beButton = 0;
			if (button == BTN_STYLUS)         beButton = 0x1;
			else if (button == BTN_STYLUS2)    beButton = 0x2;

			uint32 newButtons = obj->Buttons();
			if (state == LIBINPUT_BUTTON_STATE_PRESSED)
				newButtons |= beButton;
			else
				newButtons &= ~beButton;
			obj->UpdateButtons(newButtons);

			uint32 what = (state == LIBINPUT_BUTTON_STATE_PRESSED)
				? B_MOUSE_DOWN : B_MOUSE_UP;
			BMessage* msg = new BMessage(what);
			msg->AddPoint("where", obj->LastPosition());
			msg->AddInt32("buttons", (int32)newButtons);
			msg->AddInt32("modifiers", 0);
			msg->AddInt64("when", system_time());
			msg->AddInt32("be:device_subtype", B_TABLET_POINTING_DEVICE);
			if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
				msg->AddInt32("clicks", 1);
				msg->AddInt32("be:button", (int32)beButton);
			}
			EnqueueMessage(msg);
			break;
		}

		default:
			break;
	}
}


void
LibinputDevice::_HandleDeviceAdded(struct libinput_device* dev)
{
	bool isPointer = libinput_device_has_capability(dev,
		LIBINPUT_DEVICE_CAP_POINTER);
	bool isTouch = libinput_device_has_capability(dev,
		LIBINPUT_DEVICE_CAP_TOUCH);
	bool isTablet = libinput_device_has_capability(dev,
		LIBINPUT_DEVICE_CAP_TABLET_TOOL);

	if (!isPointer && !isTouch && !isTablet)
		return;

	debug_printf("libinput_device: device added: %s (ptr=%d touch=%d tablet=%d)\n",
		libinput_device_get_name(dev), isPointer, isTouch, isTablet);

	LibinputDeviceObject* obj = new LibinputDeviceObject(dev);

	fDeviceListLock.Lock();
	fDevices.AddItem(obj);
	fDeviceListLock.Unlock();

	input_device_ref ref;
	ref.name = (char*)obj->Name();
	ref.type = B_POINTING_DEVICE;
	ref.cookie = obj;

	input_device_ref* refs[2] = { &ref, NULL };
	RegisterDevices(refs);

	debug_printf("libinput_device: added %s\n", obj->Name());
}


void
LibinputDevice::_HandleDeviceRemoved(struct libinput_device* dev)
{
	fDeviceListLock.Lock();
	LibinputDeviceObject* obj = _FindObject(dev);
	if (obj == NULL) {
		fDeviceListLock.Unlock();
		return;
	}

	input_device_ref ref;
	ref.name = (char*)obj->Name();
	ref.type = B_POINTING_DEVICE;
	ref.cookie = obj;

	input_device_ref* refs[2] = { &ref, NULL };
	UnregisterDevices(refs);

	fDevices.RemoveItem(obj);
	delete obj;
	fDeviceListLock.Unlock();

	debug_printf("libinput_device: removed device\n");
}


LibinputDeviceObject*
LibinputDevice::_FindObject(struct libinput_device* dev) const
{
	for (int32 i = 0; i < fDevices.CountItems(); i++) {
		LibinputDeviceObject* obj = fDevices.ItemAt(i);
		if (obj->Device() == dev)
			return obj;
	}
	return NULL;
}


// Called from the merged mouse add-on
extern "C" BInputServerDevice*
instantiate_libinput_device()
{
	debug_printf("libinput_device (merged into mouse): instantiate_libinput_device() called\n");
	return new LibinputDevice();
}
