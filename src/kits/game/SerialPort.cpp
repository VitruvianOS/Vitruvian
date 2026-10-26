/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <SerialPort.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <List.h>
#include <String.h>


static const char* const kDevicePrefixes[] = {
	"ttyS", "ttyUSB", "ttyACM", "ttyAMA", NULL
};


static speed_t
speed_for_rate(data_rate rate)
{
	switch (rate) {
		case B_50_BPS:		return B50;
		case B_75_BPS:		return B75;
		case B_110_BPS:		return B110;
		case B_134_BPS:		return B134;
		case B_150_BPS:		return B150;
		case B_200_BPS:		return B200;
		case B_300_BPS:		return B300;
		case B_600_BPS:		return B600;
		case B_1200_BPS:	return B1200;
		case B_1800_BPS:	return B1800;
		case B_2400_BPS:	return B2400;
		case B_4800_BPS:	return B4800;
		case B_9600_BPS:	return B9600;
		case B_19200_BPS:	return B19200;
		case B_38400_BPS:	return B38400;
		case B_57600_BPS:	return B57600;
		case B_115200_BPS:	return B115200;
		case B_230400_BPS:	return B230400;
		default:			return B0;
	}
}


static int
compare_names(const void* a, const void* b)
{
	const BString* first = *(const BString* const*)a;
	const BString* second = *(const BString* const*)b;
	return strverscmp(first->String(), second->String());
}


BSerialPort::BSerialPort()
	:
	ffd(-1),
	fBaudRate(B_19200_BPS),
	fDataBits(B_DATA_BITS_8),
	fStopBits(B_STOP_BITS_1),
	fParityMode(B_NO_PARITY),
	fFlow(B_NOFLOW_CONTROL),
	fTimeout(B_INFINITE_TIMEOUT),
	fBlocking(true),
	fDevices(new BList)
{
}


BSerialPort::~BSerialPort()
{
	Close();

	for (int32 i = 0; i < fDevices->CountItems(); i++)
		delete (BString*)fDevices->ItemAt(i);
	delete fDevices;
}


status_t
BSerialPort::Open(const char* portName)
{
	if (portName == NULL)
		return B_BAD_VALUE;

	Close();

	BString path(portName);
	if (path.ByteAt(0) != '/')
		path.Prepend("/dev/");

	int fd = open(path.String(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;

	ffd = fd;

	struct termios options;
	if (tcgetattr(ffd, &options) != 0) {
		status_t error = errno;
		Close();
		return error;
	}

	cfmakeraw(&options);
	options.c_cflag |= CLOCAL | CREAD;
	tcsetattr(ffd, TCSANOW, &options);

	SetDataRate(fBaudRate);
	SetDataBits(fDataBits);
	SetStopBits(fStopBits);
	SetParityMode(fParityMode);
	SetFlowControl(fFlow);

	return B_OK;
}


void
BSerialPort::Close()
{
	if (ffd >= 0) {
		close(ffd);
		ffd = -1;
	}
}


ssize_t
BSerialPort::Read(void* buf, size_t count)
{
	if (ffd < 0)
		return B_FILE_ERROR;

	if (fBlocking) {
		int timeout = -1;
		if (fTimeout != B_INFINITE_TIMEOUT)
			timeout = (int)((fTimeout + 999) / 1000);

		struct pollfd pfd = { ffd, POLLIN, 0 };
		int result;
		do {
			result = poll(&pfd, 1, timeout);
		} while (result < 0 && errno == EINTR);

		if (result < 0)
			return errno;
		if (result == 0)
			return 0;
	}

	ssize_t bytes = read(ffd, buf, count);
	if (bytes < 0) {
		if (errno == EAGAIN)
			return 0;
		return errno;
	}
	return bytes;
}


ssize_t
BSerialPort::Write(const void* buf, size_t count)
{
	if (ffd < 0)
		return B_FILE_ERROR;

	const uint8* data = (const uint8*)buf;
	size_t written = 0;
	while (written < count) {
		ssize_t bytes = write(ffd, data + written, count - written);
		if (bytes < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
				if (!fBlocking)
					break;
				struct pollfd pfd = { ffd, POLLOUT, 0 };
				poll(&pfd, 1, -1);
				continue;
			}
			return written > 0 ? (ssize_t)written : (ssize_t)errno;
		}
		written += bytes;
	}
	return written;
}


void
BSerialPort::SetBlocking(bool blocking)
{
	fBlocking = blocking;
}


status_t
BSerialPort::SetTimeout(bigtime_t microSeconds)
{
	fTimeout = microSeconds;
	return B_OK;
}


status_t
BSerialPort::SetDataRate(data_rate bitsPerSecond)
{
	fBaudRate = bitsPerSecond;
	if (ffd < 0)
		return B_OK;

	struct termios options;
	if (tcgetattr(ffd, &options) != 0)
		return errno;

	speed_t speed = speed_for_rate(bitsPerSecond);
	if (bitsPerSecond == B_31250_BPS) {
		// not a standard termios rate
		return B_UNSUPPORTED;
	}
	cfsetispeed(&options, speed);
	cfsetospeed(&options, speed);
	return tcsetattr(ffd, TCSANOW, &options) == 0 ? B_OK : errno;
}


data_rate
BSerialPort::DataRate()
{
	return fBaudRate;
}


void
BSerialPort::SetDataBits(data_bits numBits)
{
	fDataBits = numBits;
	if (ffd < 0)
		return;

	struct termios options;
	if (tcgetattr(ffd, &options) != 0)
		return;

	options.c_cflag &= ~CSIZE;
	options.c_cflag |= numBits == B_DATA_BITS_7 ? CS7 : CS8;
	tcsetattr(ffd, TCSANOW, &options);
}


data_bits
BSerialPort::DataBits()
{
	return fDataBits;
}


void
BSerialPort::SetStopBits(stop_bits numBits)
{
	fStopBits = numBits;
	if (ffd < 0)
		return;

	struct termios options;
	if (tcgetattr(ffd, &options) != 0)
		return;

	if (numBits == B_STOP_BITS_2)
		options.c_cflag |= CSTOPB;
	else
		options.c_cflag &= ~CSTOPB;
	tcsetattr(ffd, TCSANOW, &options);
}


stop_bits
BSerialPort::StopBits()
{
	return fStopBits;
}


void
BSerialPort::SetParityMode(parity_mode which)
{
	fParityMode = which;
	if (ffd < 0)
		return;

	struct termios options;
	if (tcgetattr(ffd, &options) != 0)
		return;

	options.c_cflag &= ~(PARENB | PARODD);
	options.c_iflag &= ~(INPCK | ISTRIP);
	if (which != B_NO_PARITY) {
		options.c_cflag |= PARENB;
		options.c_iflag |= INPCK;
		if (which == B_ODD_PARITY)
			options.c_cflag |= PARODD;
	}
	tcsetattr(ffd, TCSANOW, &options);
}


parity_mode
BSerialPort::ParityMode()
{
	return fParityMode;
}


void
BSerialPort::ClearInput()
{
	if (ffd >= 0)
		tcflush(ffd, TCIFLUSH);
}


void
BSerialPort::ClearOutput()
{
	if (ffd >= 0)
		tcflush(ffd, TCOFLUSH);
}


void
BSerialPort::SetFlowControl(uint32 method)
{
	fFlow = method;
	if (ffd < 0)
		return;

	struct termios options;
	if (tcgetattr(ffd, &options) != 0)
		return;

	if ((method & B_HARDWARE_CONTROL) != 0)
		options.c_cflag |= CRTSCTS;
	else
		options.c_cflag &= ~CRTSCTS;

	if ((method & B_SOFTWARE_CONTROL) != 0)
		options.c_iflag |= IXON | IXOFF;
	else
		options.c_iflag &= ~(IXON | IXOFF);
	tcsetattr(ffd, TCSANOW, &options);
}


uint32
BSerialPort::FlowControl()
{
	return fFlow;
}


status_t
BSerialPort::SetDTR(bool asserted)
{
	if (ffd < 0)
		return B_FILE_ERROR;

	int bit = TIOCM_DTR;
	return ioctl(ffd, asserted ? TIOCMBIS : TIOCMBIC, &bit) == 0
		? B_OK : errno;
}


status_t
BSerialPort::SetRTS(bool asserted)
{
	if (ffd < 0)
		return B_FILE_ERROR;

	int bit = TIOCM_RTS;
	return ioctl(ffd, asserted ? TIOCMBIS : TIOCMBIC, &bit) == 0
		? B_OK : errno;
}


status_t
BSerialPort::NumCharsAvailable(int32* waitThisMany)
{
	if (ffd < 0)
		return B_FILE_ERROR;

	int available = 0;
	if (ioctl(ffd, FIONREAD, &available) != 0)
		return errno;
	*waitThisMany = available;
	return B_OK;
}


bool
BSerialPort::IsCTS()
{
	int status = 0;
	return ffd >= 0 && ioctl(ffd, TIOCMGET, &status) == 0
		&& (status & TIOCM_CTS) != 0;
}


bool
BSerialPort::IsDSR()
{
	int status = 0;
	return ffd >= 0 && ioctl(ffd, TIOCMGET, &status) == 0
		&& (status & TIOCM_DSR) != 0;
}


bool
BSerialPort::IsRI()
{
	int status = 0;
	return ffd >= 0 && ioctl(ffd, TIOCMGET, &status) == 0
		&& (status & TIOCM_RI) != 0;
}


bool
BSerialPort::IsDCD()
{
	int status = 0;
	return ffd >= 0 && ioctl(ffd, TIOCMGET, &status) == 0
		&& (status & TIOCM_CD) != 0;
}


ssize_t
BSerialPort::WaitForInput()
{
	if (ffd < 0)
		return B_FILE_ERROR;

	struct pollfd pfd = { ffd, POLLIN, 0 };
	int result;
	do {
		result = poll(&pfd, 1, -1);
	} while (result < 0 && errno == EINTR);
	if (result < 0)
		return errno;

	int available = 0;
	if (ioctl(ffd, FIONREAD, &available) != 0)
		return errno;
	return available;
}


int32
BSerialPort::CountDevices()
{
	_ScanDevices();
	return fDevices->CountItems();
}


status_t
BSerialPort::GetDeviceName(int32 index, char* name, size_t bufSize)
{
	if (name == NULL || bufSize == 0)
		return B_BAD_VALUE;

	BString* device = (BString*)fDevices->ItemAt(index);
	if (device == NULL)
		return B_BAD_INDEX;

	strlcpy(name, device->String(), bufSize);
	return B_OK;
}


void
BSerialPort::_ScanDevices()
{
	for (int32 i = 0; i < fDevices->CountItems(); i++)
		delete (BString*)fDevices->ItemAt(i);
	fDevices->MakeEmpty();

	DIR* dir = opendir("/dev");
	if (dir == NULL)
		return;

	while (struct dirent* entry = readdir(dir)) {
		for (int i = 0; kDevicePrefixes[i] != NULL; i++) {
			size_t length = strlen(kDevicePrefixes[i]);
			if (strncmp(entry->d_name, kDevicePrefixes[i], length) != 0
				|| entry->d_name[length] < '0' || entry->d_name[length] > '9')
				continue;

			if (strcmp(kDevicePrefixes[i], "ttyS") == 0) {
				// skip ports the kernel has no hardware behind
				BString path("/dev/");
				path << entry->d_name;
				int fd = open(path.String(), O_RDWR | O_NOCTTY | O_NONBLOCK);
				if (fd < 0) {
					if (errno != EACCES)
						break;
				} else {
					struct termios options;
					bool usable = tcgetattr(fd, &options) == 0;
					close(fd);
					if (!usable)
						break;
				}
			}

			fDevices->AddItem(new BString(entry->d_name));
			break;
		}
	}
	closedir(dir);

	fDevices->SortItems(compare_names);
}


int
BSerialPort::_DriverControl()
{
	return ffd;
}


void BSerialPort::_ReservedSerialPort1() {}
void BSerialPort::_ReservedSerialPort2() {}
void BSerialPort::_ReservedSerialPort3() {}
void BSerialPort::_ReservedSerialPort4() {}
