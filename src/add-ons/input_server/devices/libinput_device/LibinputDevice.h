#ifndef LIBINPUT_DEVICE_H
#define LIBINPUT_DEVICE_H

#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>

#include <InputServerDevice.h>

struct libinput;
struct libinput_device;
struct libinput_event;
struct udev;

class LibinputDeviceObject;

class LibinputDevice : public BInputServerDevice {
public:
								LibinputDevice();
	virtual						~LibinputDevice();

	virtual	status_t			InitCheck();

	virtual	status_t			Start(const char* name, void* cookie);
	virtual	status_t			Stop(const char* name, void* cookie);

	virtual	status_t			Control(const char* name, void* cookie,
									uint32 command, BMessage* message);

private:
	static	int32				_EventThreadEntry(void* data);
			int32				_EventThread();

			void				_HandleEvent(struct libinput_event* event);
			void				_HandleDeviceAdded(struct libinput_device* dev);
			void				_HandleDeviceRemoved(struct libinput_device* dev);

			LibinputDeviceObject* _FindObject(struct libinput_device* dev) const;

			status_t			fInitStatus;
			struct libinput*	fLibinput;
			struct udev*		fUdev;
			thread_id			fEventThread;
	volatile bool				fRunning;

			BObjectList<LibinputDeviceObject> fDevices;
			BLocker				fDeviceListLock;

			int32				fScreenW;
			int32				fScreenH;
			bool				fSuspended;
			bool				fStarted;
			input_device_ref	fSyntheticRef;

			float				fScrollX;
			float				fScrollY;

			// Touchscreen two-finger scroll tracking (slot 1 = second finger)
			bool				fTouchSlot1Active;
			float				fTouchSlot1LastY;
			float				fTouchSlot1LastX;
			float				fTouchScrollAccX;
			float				fTouchScrollAccY;

			// Click counting for double-click detection
			bigtime_t			fLastClickTime;
			int32				fClickCount;
			uint32				fLastClickButton;
};

class LibinputDeviceObject {
public:
								LibinputDeviceObject(struct libinput_device* dev);
								~LibinputDeviceObject();

			struct libinput_device* Device() const { return fDevice; }
			const char*			Name() const { return fName.String(); }
			uint32				Buttons() const { return fButtons; }

			void				UpdateButtons(uint32 buttons);
			BPoint				LastPosition() const { return fLastPosition; }
			void				SetLastPosition(BPoint pos) { fLastPosition = pos; }

private:
			struct libinput_device* fDevice;
			BString				fName;
			uint32				fButtons;
			BPoint				fLastPosition;
};

extern "C" BInputServerDevice* instantiate_input_device();

#endif
