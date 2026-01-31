#include <sys/epoll.h>
#include <fcntl.h>

int32
KeyboardDevice::_ControlThread()
{
	KD_CALLED();
	TRACE("fPath: %s\n", fPath);

	if (fInputHandle == NULL) {
		LOG_ERR("KeyboardDevice: Input handle is NULL.\n");
		_ControlThreadCleanup();
		return B_ERROR;
	}

	if (fFD < 0) {
		LOG_ERR("Invalid file descriptor.\n");
		_ControlThreadCleanup();
		return B_ERROR;
	}

	int epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		LOG_ERR("Failed to create epoll instance: %s\n", strerror(errno));
		_ControlThreadCleanup();
		return B_ERROR;
	}

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = fFD; // Use the file descriptor from libevdev

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
		LOG_ERR("Failed to add file descriptor to epoll: %s\n", strerror(errno));
		close(epoll_fd);
		_ControlThreadCleanup();
		return B_ERROR;
	}

	uint8 states[16] = {0};
	uint32 lastKeyCode = 0;
	uint32 repeatCount = 1;
	bool ctrlAltDelPressed = false;

	// Initialize the keyboard ID if needed
	if (ioctl(fFD, KB_GET_KEYBOARD_ID, &fKeyboardID, sizeof(fKeyboardID)) == 0) {
		BMessage message(IS_SET_KEYBOARD_ID);
		message.AddInt16("id", fKeyboardID);
		be_app->PostMessage(&message);
	}

	while (fActive) {
		struct input_event ev;
		int n = epoll_wait(epoll_fd, &event, 1, -1); // Wait indefinitely for events
		if (n < 0) {
			LOG_ERR("epoll_wait error: %s\n", strerror(errno));
			break; // Exit the loop on error
		}

		if (event.events & EPOLLIN) {
			int rc = libevdev_next_event(fInputHandle, LIBEVDEV_READ_FLAG_NORMAL, &ev);
			if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
				uint32 keycode = ev.code;
				bool isKeyDown = (ev.value == 1);
				LOG_EVENT("Key event: time=%" B_PRIdBIGTIME ", code=%02x, value=%d\n",
					ev.timestamp, keycode, ev.value);

				if (keycode == 0) continue;

				// Update states for pressed and released keys
				if (isKeyDown) {
					states[(keycode) >> 3] |= (1 << (7 - (keycode & 0x7)));
				} else {
					states[(keycode) >> 3] &= (~(1 << (7 - (keycode & 0x7))));
				}

				BMessage* msg = new BMessage(isKeyDown ? B_KEY_DOWN : B_KEY_UP);
				if (msg == NULL) continue;

				msg->AddInt64("when", ev.timestamp);
				msg->AddInt32("key", keycode);
				msg->AddInt32("modifiers", fModifiers);
				msg->AddData("states", B_UINT8_TYPE, states, sizeof(states));

				if (fOwner->EnqueueMessage(msg) != B_OK) {
					delete msg;
				}

				// Manage Ctrl + Alt + Del
				if (keycode == 0x34 && isKeyDown) { // Example: DELETE KEY
					if ((states[fCommandKey >> 3] & (1 << (7 - (fCommandKey & 0x7)))) &&
						(states[fControlKey >> 3] & (1 << (7 - (fControlKey & 0x7))))) {
						LOG_EVENT("Ctrl+Alt+Del pressed\n");
						ctrlAltDelPressed = true;
					}
				}

				if (ctrlAltDelPressed) {
					BMessage ctrlAltDelMessage(kMsgCtrlAltDelPressed);
					ctrlAltDelMessage.AddBool("key down", isKeyDown);
					fOwner->fTeamMonitorWindow->PostMessage(&ctrlAltDelDelMessage);
					if (!isKeyDown) {
						ctrlAltDelPressed = false;
					}
				}

			lastKeyCode = isKeyDown ? keycode : 0;

		} else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
			snooze(100000); // Avoid busy-looping
		} else if (rc < 0) {
			LOG_ERR("Error reading keyboard event: %s\n", strerror(-rc));
			break; // Exit the loop on error
		}
	}

	_ControlThreadCleanup();
	return 0;
}

void
KeyboardDevice::_ControlThreadCleanup()
{
	if (fActive) {
		fThread = -1;
		fOwner->_RemoveDevice(fPath);
	} else {
		// Device is already being removed; no action needed.
	}
}
