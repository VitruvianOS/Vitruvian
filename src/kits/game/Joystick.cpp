/*
 * BJoystick: Game Kit joystick class, evdev backend.
 *
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * The public API surface is the one pinned by
 * src/tests/kits/device/stickit_BJoystick (the "StickIt" sample from
 * "Getting a Grip on BJoystick").  The backend reads Linux input nodes
 * (/dev/input/event*) through the evdev ioctls provided by the
 * LinuxEvdevShim.h convention (src/add-ons/input_server/devices/), which
 * was extended with EVIOCGBIT / EVIOCGNAME / EVIOCGABS for this code.
 *
 * Notes on the mapping from evdev to the classic BJoystick model:
 * - Every EV_KEY code reported by the device counts as a button.
 * - Every EV_ABS axis is exposed as a BJoystick axis, scaled from the
 *   device's [min,max] range into the classic [-32768,32767] int16 range.
 * - Consecutive ABS_HATnX/ABS_HATnY pairs are exposed as hats; a hat
 *   value is the classic 0 (centered) or 1..8 direction pattern.
 * - One "stick" exists when both ABS_X and ABS_Y are present.
 * - Calibration and the legacy game-port "enhanced mode" have no evdev
 *   equivalent; they keep their API but report unsupported/false.
 */


#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>

#include <sys/ioctl.h>

#include <List.h>
#include <String.h>

#include <Joystick.h>

#include "LinuxEvdevShim.h"


#define JOYSTICK_DEVICE_PATH "/dev/input"
#define MAX_AXIS_VALUE		32767
#define MIN_AXIS_VALUE		(-32768)


// current state of one opened joystick device
struct JoystickDeviceData {
	int			fd;
	bool			isOpen;
	char			devicePath[128];
	BString			controllerName;

	// buttons (EV_KEY codes present on the device)
	int32			buttonCount;
	int32			buttonCodes[KEY_MAX + 1];
	bool			buttonState[KEY_MAX + 1];

	// axes (EV_ABS codes present on the device)
	int32			axisCount;
	int32			axisCodes[ABS_MAX + 1];
	int32			axisRaw[ABS_MAX + 1];
	struct input_absinfo	axisInfo[ABS_MAX + 1];

	// hats: ABS_HATnX/ABS_HATnY pairs
	int32			hatCount;

	// per-object flags that must survive Close() live in JoystickState
};


// per-BJoystick-object state kept in fJoystickData->ItemAt(0)
struct JoystickState {
	bool			calibrationEnabled;
	JoystickDeviceData*	device;

	JoystickState()
		: calibrationEnabled(false),
		  device(NULL)
	{
	}
};


static int32
find_axis_index(const JoystickDeviceData* data, int32 code)
{
	for (int32 i = 0; i < data->axisCount; i++) {
		if (data->axisCodes[i] == code)
			return i;
	}
	return -1;
}


static int32
sign_of(int32 value)
{
	return value < 0 ? -1 : (value > 0 ? 1 : 0);
}


// scale a raw evdev axis value into the classic [-32768,32767] int16 range
static int32
scale_axis_value(const JoystickDeviceData* data, int32 index)
{
	const struct input_absinfo& info = data->axisInfo[index];
	int32 range = info.maximum - info.minimum;
	if (range <= 0)
		return 0;
	int64 mid = info.minimum + range / 2;
	int64 value = (data->axisRaw[index] - mid) * 65536 / range;
	if (value > MAX_AXIS_VALUE)
		value = MAX_AXIS_VALUE;
	else if (value < MIN_AXIS_VALUE)
		value = MIN_AXIS_VALUE;
	return (int32)value;
}


// map a hat (x, y) pair in [-1, 1] to the classic 0..8 pattern,
// where evdev reports negative Y as "up".
static uint8
hat_pattern(int32 x, int32 y)
{
	x = sign_of(x);
	y = sign_of(y);

	if (x == 0 && y == 0)
		return 0;
	if (x == 0 && y < 0)
		return 1;	// north
	if (x > 0 && y < 0)
		return 2;	// northeast
	if (x > 0 && y == 0)
		return 3;	// east
	if (x > 0 && y > 0)
		return 4;	// southeast
	if (x == 0 && y > 0)
		return 5;	// south
	if (x < 0 && y > 0)
		return 6;	// southwest
	if (x < 0 && y == 0)
		return 7;	// west
	return 8;		// northwest
}


static int32
count_hat_pairs(const JoystickDeviceData* data)
{
	int32 count = 0;
	for (int32 hat = 0; hat <= (ABS_HAT3Y - ABS_HAT0X) / 2; hat++) {
		int32 xCode = ABS_HAT0X + hat * 2;
		int32 yCode = ABS_HAT0Y + hat * 2;
		if (find_axis_index(data, xCode) >= 0
			&& find_axis_index(data, yCode) >= 0)
			count++;
	}
	return count;
}


static const char*
axis_name_for_code(int32 code)
{
	switch (code) {
		case ABS_X:		return "X Axis";
		case ABS_Y:		return "Y Axis";
		case ABS_Z:		return "Z Axis";
		case ABS_RX:	return "X Rotation";
		case ABS_RY:	return "Y Rotation";
		case ABS_RZ:	return "Z Rotation";
		case ABS_THROTTLE:	return "Throttle";
		case ABS_RUDDER:	return "Rudder";
		case ABS_WHEEL:	return "Wheel";
		case ABS_GAS:	return "Gas";
		case ABS_BRAKE:	return "Brake";
		case ABS_HAT0X:	return "Hat0 X";
		case ABS_HAT0Y:	return "Hat0 Y";
		case ABS_HAT1X:	return "Hat1 X";
		case ABS_HAT1Y:	return "Hat1 Y";
		case ABS_HAT2X:	return "Hat2 X";
		case ABS_HAT2Y:	return "Hat2 Y";
		case ABS_HAT3X:	return "Hat3 X";
		case ABS_HAT3Y:	return "Hat3 Y";
		case ABS_PRESSURE:	return "Pressure";
		case ABS_DISTANCE:	return "Distance";
		case ABS_VOLUME:	return "Volume";
		default:		return NULL;
	}
}


static const char*
button_name_for_code(int32 code)
{
	switch (code) {
		/* BTN_MISC aliases BTN_0 (0x100); it is named via BTN_0 below */
		case BTN_0:		return "Button 0";
		case BTN_1:		return "Button 1";
		case BTN_2:		return "Button 2";
		case BTN_3:		return "Button 3";
		case BTN_4:		return "Button 4";
		case BTN_5:		return "Button 5";
		case BTN_6:		return "Button 6";
		case BTN_7:		return "Button 7";
		case BTN_8:		return "Button 8";
		case BTN_9:		return "Button 9";
		case BTN_MOUSE:	return "Left";
		case BTN_RIGHT:	return "Right";
		case BTN_MIDDLE:	return "Middle";
		case BTN_SIDE:	return "Side";
		case BTN_EXTRA:	return "Extra";
		case BTN_FORWARD:	return "Forward";
		case BTN_BACK:	return "Back";
		case BTN_TASK:	return "Task";
		case BTN_TRIGGER:	return "Trigger";
		case BTN_THUMB:	return "Thumb";
		case BTN_THUMB2:	return "Thumb2";
		case BTN_TOP:		return "Top";
		case BTN_TOP2:	return "Top2";
		case BTN_PINKIE:	return "Pinkie";
		case BTN_BASE:	return "Base";
		case BTN_BASE2:	return "Base2";
		case BTN_BASE3:	return "Base3";
		case BTN_BASE4:	return "Base4";
		case BTN_BASE5:	return "Base5";
		case BTN_BASE6:	return "Base6";
		case BTN_DEAD:	return "Dead";
		case BTN_A:		return "A Button";
		case BTN_B:		return "B Button";
		case BTN_C:		return "C Button";
		case BTN_X:		return "X Button";
		case BTN_Y:		return "Y Button";
		case BTN_Z:		return "Z Button";
		case BTN_TL:	return "TL Button";
		case BTN_TR:	return "TR Button";
		case BTN_TL2:	return "TL2 Button";
		case BTN_TR2:	return "TR2 Button";
		case BTN_SELECT:	return "Select";
		case BTN_START:	return "Start";
		case BTN_MODE:	return "Mode";
		case BTN_THUMBL:	return "Thumb L";
		case BTN_THUMBR:	return "Thumb R";
		default:		return NULL;
	}
}


static bool
test_bit(const unsigned char* bits, int32 code)
{
	return (bits[code / 8] & (1 << (code % 8))) != 0;
}


// Does this fd describe a joystick or gamepad? Like udev's ID_INPUT_JOYSTICK, it needs an
// axis plus a joystick, gamepad or trigger-happy button, which keeps touchpads and tablets out.
static bool
is_joystick_fd(int fd, char* nameBuffer, size_t nameBufferSize)
{
	unsigned char keyBits[(KEY_MAX + 7) / 8];
	memset(keyBits, 0, sizeof(keyBits));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0)
		return false;
	if (test_bit(keyBits, BTN_TOUCH))
		return false;

	bool hasButton = false;
	for (int32 code = BTN_JOYSTICK; code < BTN_DIGI && !hasButton; code++)
		hasButton = test_bit(keyBits, code);
	for (int32 code = BTN_TRIGGER_HAPPY1; code <= BTN_TRIGGER_HAPPY40
			&& !hasButton; code++)
		hasButton = test_bit(keyBits, code);
	if (!hasButton)
		return false;

	unsigned char absBits[(ABS_MAX + 7) / 8];
	memset(absBits, 0, sizeof(absBits));
	if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0)
		return false;

	bool hasAxis = false;
	for (size_t i = 0; i < sizeof(absBits); i++) {
		if (absBits[i] != 0) {
			hasAxis = true;
			break;
		}
	}
	if (!hasAxis)
		return false;

	if (nameBuffer != NULL && nameBufferSize > 0) {
		nameBuffer[0] = '\0';
		ioctl(fd, EVIOCGNAME(nameBufferSize - 1), nameBuffer);
	}
	return true;
}


BJoystick::BJoystick()
	: timestamp(0),
	  horizontal(0),
	  vertical(0),
	  button1(false),
	  button2(false),
	  fBeBoxMode(false),
	  fReservedBool(false),
	  fFD(-1),
	  fDevices(NULL),
	  fJoystickInfo(NULL),
	  fJoystickData(NULL)
{
	memset(_reserved_Joystick_, 0, sizeof(_reserved_Joystick_));

	fDevices = new(std::nothrow) BList;
	fJoystickData = new(std::nothrow) BList(1);
	if (fDevices == NULL || fJoystickData == NULL)
		return;

	fJoystickData->AddItem(new(std::nothrow) JoystickState);

	ScanDevices();
}


BJoystick::~BJoystick()
{
	Close();

	if (fDevices != NULL) {
		for (int32 i = 0; i < fDevices->CountItems(); i++)
			free(fDevices->ItemAt(i));
		fDevices->MakeEmpty();
		delete fDevices;
	}
	if (fJoystickData != NULL) {
		delete (JoystickState*)fJoystickData->ItemAt(0);
		fJoystickData->MakeEmpty();
		delete fJoystickData;
	}

	fDevices = NULL;
	fJoystickData = NULL;
}


void
BJoystick::ScanDevices(bool useDisabled)
{
	// (re)build the device name list from /dev/input/event*

	if (fDevices == NULL)
		return;

	for (int32 i = 0; i < fDevices->CountItems(); i++)
		free(fDevices->ItemAt(i));
	fDevices->MakeEmpty();

	DIR* dir = opendir(JOYSTICK_DEVICE_PATH);
	if (dir == NULL)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, "event", 5) != 0)
			continue;

		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", JOYSTICK_DEVICE_PATH,
			entry->d_name);

		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;

		char name[128];
		if (is_joystick_fd(fd, name, sizeof(name)))
			fDevices->AddItem(strdup(path));

		close(fd);
	}

	closedir(dir);
}


int32
BJoystick::CountDevices()
{
	return fDevices != NULL ? fDevices->CountItems() : 0;
}


status_t
BJoystick::GetDeviceName(int32 index, char* name, size_t bufSize)
{
	if (fDevices == NULL || index < 0 || index >= fDevices->CountItems()
		|| name == NULL || bufSize == 0)
		return B_ERROR;

	const char* path = (const char*)fDevices->ItemAt(index);
	if (path == NULL)
		return B_ERROR;

	strlcpy(name, path, bufSize);
	return B_OK;
}


status_t
BJoystick::RescanDevices()
{
	ScanDevices();
	return B_OK;
}


status_t
BJoystick::Open(const char* portName)
{
	if (portName == NULL)
		return B_ERROR;

	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL)
		return B_NO_INIT;

	// only one device at a time
	Close();

	int fd = open(portName, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return B_ERROR;

	JoystickDeviceData* data = new(std::nothrow) JoystickDeviceData();
	if (data == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	data->fd = fd;
	data->isOpen = true;
	strlcpy(data->devicePath, portName, sizeof(data->devicePath));

	// controller name via EVIOCGNAME
	char name[128];
	name[0] = '\0';
	if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0)
		data->controllerName = name;
	else
		data->controllerName = "Unknown";

	// buttons: every EV_KEY code reported by the device
	unsigned char keyBits[(KEY_MAX + 7) / 8];
	memset(keyBits, 0, sizeof(keyBits));
	data->buttonCount = 0;
	memset(data->buttonState, 0, sizeof(data->buttonState));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
		for (int32 code = 0; code <= KEY_MAX; code++) {
			if (keyBits[code / 8] & (1 << (code % 8))) {
				data->buttonCodes[data->buttonCount++] = code;
			}
		}
	}

	// axes: every EV_ABS code, with its calibration range
	unsigned char absBits[(ABS_MAX + 7) / 8];
	memset(absBits, 0, sizeof(absBits));
	data->axisCount = 0;
	if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0) {
		for (int32 code = 0; code <= ABS_MAX; code++) {
			if (absBits[code / 8] & (1 << (code % 8))) {
				struct input_absinfo info;
				memset(&info, 0, sizeof(info));
				ioctl(fd, EVIOCGABS(code), &info);
				data->axisInfo[data->axisCount] = info;
				data->axisCodes[data->axisCount] = code;
				data->axisRaw[data->axisCount] = info.value;
				data->axisCount++;
			}
		}
	}

	data->hatCount = count_hat_pairs(data);

	state->device = data;
	fFD = fd;
	fJoystickData->AddItem(data);

	timestamp = system_time();
	Update();

	return B_OK;
}


status_t
BJoystick::Open(const char* portName, bool enhanced)
{
	// no legacy/enhanced distinction on evdev
	return Open(portName);
}


void
BJoystick::Close()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL)
		return;

	if (state->device != NULL) {
		JoystickDeviceData* data = state->device;
		if (data->isOpen && data->fd >= 0)
			close(data->fd);
		data->fd = -1;
		data->isOpen = false;

		// the data item is also in the list; find and drop it
		for (int32 i = fJoystickData->CountItems() - 1; i > 0; i--) {
			if (fJoystickData->ItemAt(i) == data)
				fJoystickData->RemoveItem(i);
		}
		delete data;
		state->device = NULL;
	}

	fFD = -1;
	horizontal = 0;
	vertical = 0;
	button1 = false;
	button2 = false;
}


status_t
BJoystick::Update()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL)
		return B_ERROR;

	JoystickDeviceData* data = state->device;
	if (!data->isOpen)
		return B_ERROR;

	bool gotEvents = false;
	struct input_event event;
	while (true) {
		ssize_t amount = read(data->fd, &event, sizeof(event));
		if (amount != (ssize_t)sizeof(event)) {
			if (amount < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				return B_ERROR;
			break;
		}
		gotEvents = true;

		switch (event.type) {
			case EV_KEY:
				if (event.code >= 0 && event.code <= KEY_MAX)
					data->buttonState[event.code] = event.value != 0;
				break;
			case EV_ABS:
			{
				int32 index = find_axis_index(data, event.code);
				if (index >= 0)
					data->axisRaw[index] = event.value;
				break;
			}
			case EV_SYN:
				timestamp = system_time();
				break;
			default:
				break;
		}
	}

	if (gotEvents) {
		// refresh the legacy direct-access fields
		int32 xIndex = find_axis_index(data, ABS_X);
		int32 yIndex = find_axis_index(data, ABS_Y);
		horizontal = xIndex >= 0 ? (int16)scale_axis_value(data, xIndex) : 0;
		vertical = yIndex >= 0 ? (int16)scale_axis_value(data, yIndex) : 0;
		button1 = data->buttonCount > 0
			? data->buttonState[data->buttonCodes[0]] : false;
		button2 = data->buttonCount > 1
			? data->buttonState[data->buttonCodes[1]] : false;
	}

	return B_OK;
}


int32
BJoystick::CountSticks()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL)
		return 0;

	JoystickDeviceData* data = state->device;
	if (find_axis_index(data, ABS_X) >= 0 && find_axis_index(data, ABS_Y) >= 0)
		return 1;
	return 0;
}


int32
BJoystick::CountAxes()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL)
		return 0;
	return state->device->axisCount;
}


int32
BJoystick::CountHats()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL)
		return 0;
	return state->device->hatCount;
}


int32
BJoystick::CountButtons()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL)
		return 0;
	return state->device->buttonCount;
}


status_t
BJoystick::GetAxisValues(int16* outValues, int32 forStick)
{
	if (forStick != 0)
		return B_ERROR;

	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outValues == NULL)
		return B_ERROR;

	JoystickDeviceData* data = state->device;
	for (int32 i = 0; i < data->axisCount; i++)
		outValues[i] = (int16)scale_axis_value(data, i);
	return B_OK;
}


status_t
BJoystick::GetButtonValues(bool* outButtons, int32 forStick)
{
	if (forStick != 0)
		return B_ERROR;

	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outButtons == NULL)
		return B_ERROR;

	JoystickDeviceData* data = state->device;
	for (int32 i = 0; i < data->buttonCount; i++)
		outButtons[i] = data->buttonState[data->buttonCodes[i]];
	return B_OK;
}


uint32
BJoystick::ButtonValues(int32 forStick)
{
	if (forStick != 0)
		return 0;

	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL)
		return 0;

	JoystickDeviceData* data = state->device;
	uint32 values = 0;
	int32 count = data->buttonCount < 32 ? data->buttonCount : 32;
	for (int32 i = 0; i < count; i++) {
		if (data->buttonState[data->buttonCodes[i]])
			values |= 1 << i;
	}
	return values;
}


status_t
BJoystick::GetHatValues(uint8* outHats, int32 forStick)
{
	if (forStick != 0)
		return B_ERROR;

	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outHats == NULL)
		return B_ERROR;

	JoystickDeviceData* data = state->device;
	for (int32 hat = 0; hat < data->hatCount; hat++) {
		int32 xIndex = find_axis_index(data, ABS_HAT0X + hat * 2);
		int32 yIndex = find_axis_index(data, ABS_HAT0Y + hat * 2);
		int32 x = xIndex >= 0 ? data->axisRaw[xIndex] : 0;
		int32 y = yIndex >= 0 ? data->axisRaw[yIndex] : 0;
		outHats[hat] = hat_pattern(x, y);
	}
	return B_OK;
}


status_t
BJoystick::GetAxisNameAt(int32 index, BString* outName)
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outName == NULL
		|| index < 0 || index >= state->device->axisCount)
		return B_ERROR;

	int32 code = state->device->axisCodes[index];
	const char* name = axis_name_for_code(code);
	if (name == NULL) {
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "Axis %d", (int)index + 1);
		*outName = buffer;
	} else
		*outName = name;
	return B_OK;
}


status_t
BJoystick::GetButtonNameAt(int32 index, BString* outName)
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outName == NULL
		|| index < 0 || index >= state->device->buttonCount)
		return B_ERROR;

	int32 code = state->device->buttonCodes[index];
	const char* name = button_name_for_code(code);
	if (name == NULL) {
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "Button %d", (int)index + 1);
		*outName = buffer;
	} else
		*outName = name;
	return B_OK;
}


status_t
BJoystick::GetHatNameAt(int32 index, BString* outName)
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outName == NULL
		|| index < 0 || index >= state->device->hatCount)
		return B_ERROR;

	char buffer[64];
	snprintf(buffer, sizeof(buffer), "Hat %d", (int)index);
	*outName = buffer;
	return B_OK;
}


status_t
BJoystick::GetControllerName(BString* outName)
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL || state->device == NULL || outName == NULL)
		return B_ERROR;

	*outName = state->device->controllerName;
	return B_OK;
}


status_t
BJoystick::GetControllerModule(BString* outName)
{
	if (outName == NULL)
		return B_ERROR;

	*outName = "Evdev";
	return B_OK;
}


bool
BJoystick::EnterEnhancedMode(const entry_ref* ref)
{
	// The legacy game-port enhanced mode has no evdev equivalent.
	return false;
}


bool
BJoystick::IsCalibrationEnabled()
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	return state != NULL ? state->calibrationEnabled : false;
}


status_t
BJoystick::EnableCalibration(bool calibrates)
{
	JoystickState* state = fJoystickData != NULL
		? (JoystickState*)fJoystickData->ItemAt(0) : NULL;
	if (state == NULL)
		return B_NO_INIT;

	state->calibrationEnabled = calibrates;
	return B_OK;
}


void
BJoystick::Calibrate(struct _extended_joystick*)
{
	// no calibration pass-through in the evdev backend
}


status_t
BJoystick::SetMaxLatency(bigtime_t maxLatency)
{
	// evdev pushes events as they happen; there is no latency knob.
	return B_ERROR;
}


// Reserved slots of the restored ABI, never called.
void
BJoystick::_ReservedJoystick2()
{
}


void
BJoystick::_ReservedJoystick3()
{
}


status_t
BJoystick::_Reserved_Joystick_4(void*, ...)
{
	return B_ERROR;
}


status_t
BJoystick::_Reserved_Joystick_5(void*, ...)
{
	return B_ERROR;
}


status_t
BJoystick::_Reserved_Joystick_6(void*, ...)
{
	return B_ERROR;
}
