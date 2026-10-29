/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LsDiscovery.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "LsJson.h"

namespace LocalSend {

static const size_t kMaxPacketSize = 65507;


LsDiscovery::LsDiscovery()
	:
	fSocket(-1),
	fPort(0),
	fThread(0),
	fRunning(false),
	fThreadStarted(false),
	fListener(NULL)
{
}


LsDiscovery::~LsDiscovery()
{
	Stop();
	if (fSocket >= 0)
		close(fSocket);
}


int
LsDiscovery::Init(uint16_t port, const std::string& group)
{
	fPort = port;
	fGroup = group;

	fSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (fSocket < 0)
		return -1;

	int one = 1;
	setsockopt(fSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(fSocket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

	struct sockaddr_in bindAddress;
	memset(&bindAddress, 0, sizeof(bindAddress));
	bindAddress.sin_family = AF_INET;
	bindAddress.sin_port = htons(port);
	bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fSocket, reinterpret_cast<struct sockaddr*>(&bindAddress),
			sizeof(bindAddress)) != 0) {
		close(fSocket);
		fSocket = -1;
		return -1;
	}

	struct ip_mreq membership;
	membership.imr_multiaddr.s_addr = inet_addr(group.c_str());
	membership.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(fSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
			sizeof(membership)) != 0) {
		// Membership failure is not fatal for tests over loopback;
		// sending still works.
	}

	int loopback = 1;
	setsockopt(fSocket, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback,
		sizeof(loopback));

	return 0;
}


int
LsDiscovery::Start(LsDiscoveryListener* listener)
{
	if (fSocket < 0 || fRunning)
		return -1;
	fListener = listener;

	fRunning = true;
	int status = pthread_create(&fThread, NULL, &LsDiscovery::_ThreadEntry,
		this);
	if (status != 0) {
		fRunning = false;
		return -1;
	}
	fThreadStarted = true;
	return 0;
}


void
LsDiscovery::Stop()
{
	if (!fRunning && !fThreadStarted)
		return;

	fRunning = false;
	// Wake the blocking recv by shutting the socket down.
	if (fSocket >= 0)
		shutdown(fSocket, SHUT_RDWR);

	if (fThreadStarted) {
		pthread_join(fThread, NULL);
		fThreadStarted = false;
	}
}


int
LsDiscovery::Announce(const LsDevice& device, bool announceFlag)
{
	if (fSocket < 0)
		return -1;

	LsAnnounceMessage message;
	message.device = device;
	message.announce = announceFlag;
	std::string payload = message.ToJson().Serialize();
	if (payload.size() > kMaxPacketSize)
		return -1;

	struct sockaddr_in target;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(fPort);
	target.sin_addr.s_addr = inet_addr(fGroup.c_str());

	ssize_t sent = sendto(fSocket, payload.c_str(), payload.size(), 0,
		reinterpret_cast<struct sockaddr*>(&target), sizeof(target));
	return sent == static_cast<ssize_t>(payload.size()) ? 0 : -1;
}


/* static */ bool
LsDiscovery::ParsePacket(const char* data, size_t length,
	LsAnnounceMessage& message)
{
	if (data == NULL || length == 0 || length > kMaxPacketSize)
		return false;

	std::string text(data, length);
	LsJson json;
	if (!LsJson::Parse(text, json))
		return false;
	return LsAnnounceMessage::FromJson(json, message);
}


/* static */ void*
LsDiscovery::_ThreadEntry(void* data)
{
	static_cast<LsDiscovery*>(data)->_ReadLoop();
	return NULL;
}


void
LsDiscovery::_ReadLoop()
{
	char buffer[kMaxPacketSize + 1];

	while (fRunning) {
		// Bounded wait: shutdown() alone does not reliably wake a
		// blocked recvfrom, and Stop() joins this thread.
		struct pollfd descriptor;
		descriptor.fd = fSocket;
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		int status = poll(&descriptor, 1, 500);
		if (status < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (status == 0 || (descriptor.revents & POLLIN) == 0)
			continue;

		struct sockaddr_in source;
		socklen_t sourceLength = sizeof(source);
		ssize_t received = recvfrom(fSocket, buffer, kMaxPacketSize, 0,
			reinterpret_cast<struct sockaddr*>(&source), &sourceLength);
		if (received < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		LsAnnounceMessage message;
		if (!ParsePacket(buffer, static_cast<size_t>(received), message))
			continue;
		if (message.device.fingerprint.empty())
			continue;

		char address[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &source.sin_addr, address,
				sizeof(address)) == NULL)
			continue;

		if (fListener != NULL) {
			fListener->DeviceAnnounced(message.device, address,
				message.announce);
		}
	}
}

} // namespace LocalSend
