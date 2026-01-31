void
MouseDevice::_ControlThread()
{
	MD_CALLED();

	printf("control thread\n");

	uint32 lastButtons = 0;
	float historyDeltaX = 0.0;
	float historyDeltaY = 0.0;

	static const bigtime_t kTransferDelay = 1000000 / 125;
#define USE_REGULAR_INTERVAL 0
#if USE_REGULAR_INTERVAL
	bigtime_t nextTransferTime = system_time() + kTransferDelay;
#endif

	int evdev_fd;  // File descriptor for the evdev device
	struct libevdev *dev = NULL;
	int ret = libevdev_new_from_fd(fDevice, &dev);
	if (ret < 0) {
		fprintf(stderr, "Failed to init libevdev: %s\n", strerror(-ret));
		return;
	}

	libevdev_grab(dev, LIBEVDEV_GRAB_MODE_NONBLOCK);

	while (fActive) {
		mouse_movement movements;

#if USE_REGULAR_INTERVAL
		snooze_until(nextTransferTime, B_SYSTEM_TIMEBASE);
		nextTransferTime += kTransferDelay;
#endif

		memset(&movements, 0, sizeof(movements));

		ret = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &movements);
		if (ret == LIBEVDEV_READ_STATUS_SUCCESS) {
			switch (movements.type) {
				case EV_REL:
					if (movements.code == REL_X) {
						movements.xdelta = movements.value;
					} else if (movements.code == REL_Y) {
						movements.ydelta = -movements.value;  // Invert Y axis
					}
					break;

				case EV_KEY:
					if (movements.value == 1) {  // Key pressed
						movements.clicks++;
						switch (movements.code) {
							case BTN_LEFT:   movements.buttons = 0x1; break;
							case BTN_RIGHT:  movements.buttons = 0x2; break;
							case BTN_MIDDLE: movements.buttons = 0x3; break;
						}
					}
					break;

				case EV_ABS:
					// Handle if necessary for touchpads
					break;

				default:
					continue;
			}

			movements.timestamp = system_time();
			uint32 buttons = lastButtons ^ movements.buttons;
			uint32 remappedButtons = _RemapButtons(movements.buttons;

			int32 deltaX, deltaY;
			_ComputeAcceleration(movements, deltaX, deltaY, historyDeltaX, historyDeltaY);

			if (buttons != 0) {
				bool pressedButton = (buttons & movements.buttons) > 0;
				BMessage* message = _BuildMouseMessage(pressedButton ? B_MOUSE_DOWN : B_MOUSE_UP,
					movements.timestamp, remappedButtons, deltaX, deltaY);
				if (message != NULL) {
					if (pressedButton) {
						message->AddInt32("clicks", movements.clicks);
					}
					fTarget.EnqueueMessage(message);
					lastButtons = movements.buttons;
				}
			}

			if (movements.xdelta != 0 || movements.ydelta != 0) {
				BMessage* message = _BuildMouseMessage(B_MOUSE_MOVED,
					movements.timestamp, remappedButtons, deltaX, deltaY);
				if (message != NULL) {
					fTarget.EnqueueMessage(message);
				}
			}

			if (movements.wheel_ydelta != 0 || movements.wheel_xdelta != 0) {
				BMessage* message = new BMessage(B_MOUSE_WHEEL_CHANGED);
				if (message == NULL)
					continue;

				if (message->AddInt64("when", movements.timestamp) == B_OK
					&& message->AddFloat("be:wheel_delta_x", movements.wheel_xdelta) == B_OK
					&& message->AddFloat("be:wheel_delta_y", movements.wheel_ydelta) == B_OK) {
					fTarget.EnqueueMessage(message);
				} else {
					delete message;
				}
			}
		}
	}

	libevdev_free(dev);
}
