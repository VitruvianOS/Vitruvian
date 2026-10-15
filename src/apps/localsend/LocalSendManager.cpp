/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LocalSendManager.h"

#include <OS.h>
#include <String.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "BeHttpTransport.h"
#include "LocalSendMessages.h"
#include "LsFingerprint.h"
#include "LsHttpTransport.h"
#include "LsJson.h"
#include "LsProtocol.h"
#include "LsSenderSession.h"
#include "LsSha256.h"

namespace ls = LocalSend;


static const bigtime_t kPromptTimeout = 60 * 1000000;


LocalSendManager::LocalSendManager(LocalSendSettings* settings)
	:
	fSettings(settings),
	fSending(false),
	fPendingArgs(NULL),
	fPromptSemaphore(-1),
	fPromptPending(false),
	fPromptAccept(false),
	fPromptAlwaysAllow(false)
{
	fPromptSemaphore = create_sem(0, "LocalSend prompt");
}


LocalSendManager::~LocalSendManager()
{
	Stop();
	delete fPendingArgs;
	delete_sem(fPromptSemaphore);
}


uint16_t
LocalSendManager::ServerPort() const
{
	return fServer.Port();
}


status_t
LocalSendManager::Start()
{
	// Stable identity across runs.
	if (fSettings->Fingerprint().IsEmpty())
		fSettings->SetFingerprint(
			BString(ls::LsGenerateFingerprint().c_str()));

	fDevice.alias = fSettings->Alias().String();
	fDevice.version = ls::kProtocolVersion;
	fDevice.deviceModel = "VOS";
	fDevice.deviceType = ls::kDeviceTypeDesktop;
	fDevice.fingerprint = fSettings->Fingerprint().String();
	fDevice.protocol = ls::kProtocolHttp;
	fDevice.download = false;

	if (fServer.Init(ls::kDefaultPort) != 0)
		return B_ERROR;

	fDevice.port = fServer.Port();

	fReceiver.SetDestination(fSettings->DestinationFolder().String());
	fReceiver.SetPrompt(this);
	fReceiver.SetRequiredPin(fSettings->RequirePin()
		? fSettings->Pin().String() : "");

	if (fServer.Start(&fReceiver, fDevice) != 0)
		return B_ERROR;

	if (fDiscovery.Init() != 0)
		return B_ERROR;
	if (fDiscovery.Start(this) != 0)
		return B_ERROR;

	// Opening burst so peers that start listening slightly later
	// still see us, like the reference implementation.
	for (int i = 0; i < 3; i++) {
		fDiscovery.Announce(fDevice, true);
		snooze(250000);
	}

	// HTTP fallback discovery (protocol section 3.2): probe the
	// local subnets when multicast may be filtered.
	thread_id scanThread = spawn_thread(&LocalSendManager::_ScanThreadEntry,
		"LocalSend scan", B_NORMAL_PRIORITY, this);
	if (scanThread >= B_OK)
		resume_thread(scanThread);

	return B_OK;
}


void
LocalSendManager::Stop()
{
	fDiscovery.Stop();
	fServer.Stop();

	// Unblock a prompt stuck in Prompt() so the worker can exit.
	release_sem(fPromptSemaphore);
}


void
LocalSendManager::DeviceAnnounced(const ls::LsDevice& device,
	const std::string& sourceAddress, bool announceFlag)
{
	// Never list ourselves; the loopback socket makes this common
	// when two instances run on one machine.
	if (ls::LsFingerprintsEqual(device.fingerprint,
			fSettings->Fingerprint().String()))
		return;

	if (fWindowMessenger.IsValid()) {
		BMessage message(kMsgDeviceUpdate);
		message.AddString("alias", device.alias.c_str());
		message.AddString("model", device.deviceModel.c_str());
		message.AddString("fingerprint", device.fingerprint.c_str());
		message.AddString("address", sourceAddress.c_str());
		message.AddString("typeName",
			ls::LsDevice::DeviceTypeName(device.deviceType));
		message.AddString("version", device.version.c_str());
		message.AddUInt16("port", device.port);
		message.AddString("protocol",
			ls::LsDevice::ProtocolTypeName(device.protocol));
		message.AddBool("download", device.download);
		fWindowMessenger.SendMessage(&message);
	}

	// Spec section 3.1: answer an announce with a unicast /register
	// POST to the origin. announce=false packets need no answer.
	if (!announceFlag)
		return;

	ls::BeHttpTransport transport;
	std::string path = ls::kApiBase;
	path += ls::kEndpointRegister;
	transport.PostJson(sourceAddress, device.port, path,
		fDevice.ToJson().Serialize());
}


ls::LsReceiveDecision
LocalSendManager::Prompt(const ls::LsDevice& sender,
	const std::map<std::string, ls::LsFileItem>& files,
	const std::string& /*pin*/, std::vector<std::string>& acceptedFiles)
{
	ls::LsReceiveDecision decision;

	// Fast path: always-allowed device, no prompt.
	if (fSettings->IsTrusted(BString(sender.fingerprint.c_str()))) {
		decision.accepted = true;
		decision.autoAccepted = true;
		return decision;
	}

	fPromptLock.Lock();
	fPromptAccept = false;
	fPromptAlwaysAllow = false;
	fPromptAcceptedFiles.clear();
	fPromptSender = sender;
	fPromptPending = true;
	fPromptLock.Unlock();

	if (fWindowMessenger.IsValid()) {
		BMessage message(kMsgReceivePrompt);
		message.AddString("alias", sender.alias.c_str());
		for (std::map<std::string, ls::LsFileItem>::const_iterator it
				= files.begin(); it != files.end(); it++) {
			message.AddString("file", it->second.fileName.c_str());
			message.AddInt64("size", it->second.size);
		}
		fWindowMessenger.SendMessage(&message);
	}

	// Block the server worker until the UI answers or the sender
	// gives up waiting (spec: unanswered prepare is a 403).
	status_t status = acquire_sem_etc(fPromptSemaphore, 1,
		B_RELATIVE_TIMEOUT, kPromptTimeout);
	fPromptPending = false;
	if (status != B_OK)
		return decision;

	fPromptLock.Lock();
	decision.accepted = fPromptAccept;
	if (fPromptAccept && !fPromptAcceptedFiles.empty())
		acceptedFiles = fPromptAcceptedFiles;
	fPromptLock.Unlock();
	return decision;
}


void
LocalSendManager::AnswerPrompt(bool accept, bool alwaysAllow,
	const std::vector<BString>& fileNames)
{
	fPromptLock.Lock();
	fPromptAccept = accept;
	fPromptAlwaysAllow = alwaysAllow;
	fPromptAcceptedFiles.clear();
	for (size_t i = 0; i < fileNames.size(); i++)
		fPromptAcceptedFiles.push_back(fileNames[i].String());
	BString fingerprint(fPromptSender.fingerprint.c_str());
	fPromptLock.Unlock();

	if (alwaysAllow && accept) {
		fSettings->AddTrusted(fingerprint);
		fSettings->Save();
	}

	release_sem(fPromptSemaphore);
}


void
LocalSendManager::SendFiles(const std::vector<BString>& paths,
	const BString& address, const ls::LsDevice& device)
{
	fSendLock.Lock();
	if (fSending) {
		fSendLock.Unlock();
		return;
	}
	fSending = true;
	fSendLock.Unlock();

	SendArgs* args = new SendArgs;
	args->paths = paths;
	args->address = address;
	args->device = device;

	thread_id thread = spawn_thread(&LocalSendManager::_SendThreadEntry,
		"LocalSend send", B_NORMAL_PRIORITY, this);
	if (thread < B_OK) {
		delete args;
		fSendLock.Lock();
		fSending = false;
		fSendLock.Unlock();
		return;
	}

	// The entry only gets "this"; stash args on the instance under
	// the send lock. One send at a time: the UI disables Send while
	// one is running, and SendFiles refuses above.
	fPendingArgs = args;
	resume_thread(thread);
}


int32_t
LocalSendManager::_SendThreadEntry(void* data)
{
	static_cast<LocalSendManager*>(data)->_SendThread(NULL);
	return 0;
}


int32_t
LocalSendManager::_ScanThreadEntry(void* data)
{
	static_cast<LocalSendManager*>(data)->_ScanSubnets();
	return 0;
}


void
LocalSendManager::_ScanSubnets()
{
	// Protocol section 3.2: POST /register to every address of the
	// local IPv4 subnets. A short timeout keeps dead addresses from
	// stalling; the scan itself runs off the UI thread.
	struct ifaddrs* interfaces = NULL;
	if (getifaddrs(&interfaces) != 0)
		return;

	// Collect the /24 prefixes of up to 3 non-loopback IPv4
	// interfaces, like the reference app's maxInterfaces.
	std::vector<uint32_t> prefixes;
	for (struct ifaddrs* current = interfaces; current != NULL
		&& prefixes.size() < 3; current = current->ifa_next) {
		if (current->ifa_addr == NULL
			|| current->ifa_addr->sa_family != AF_INET)
			continue;
		if ((current->ifa_flags & IFF_UP) == 0
			|| (current->ifa_flags & IFF_LOOPBACK) != 0)
			continue;

		uint32_t address = ntohl(reinterpret_cast<struct sockaddr_in*>(
			current->ifa_addr)->sin_addr.s_addr);
		uint32_t prefix = address & 0xffffff00;
		bool duplicate = false;
		for (size_t i = 0; i < prefixes.size(); i++) {
			if (prefixes[i] == prefix)
				duplicate = true;
		}
		if (!duplicate)
			prefixes.push_back(prefix);
	}
	freeifaddrs(interfaces);

	if (prefixes.empty())
		return;

	ls::LsSocketTransport transport;
	transport.SetTimeoutMs(300);

	std::string path = ls::kApiBase;
	path += ls::kEndpointRegister;
	std::string body = fDevice.ToJson().Serialize();

	for (size_t p = 0; p < prefixes.size(); p++) {
		for (uint32_t host = 1; host < 255; host++) {
			uint32_t addressValue = prefixes[p] | host;
			char address[INET_ADDRSTRLEN];
			snprintf(address, sizeof(address), "%u.%u.%u.%u",
				(addressValue >> 24) & 0xff,
				(addressValue >> 16) & 0xff,
				(addressValue >> 8) & 0xff,
				addressValue & 0xff);

			ls::LsHttpResult result = transport.PostJson(address,
				ls::kDefaultPort, path, body);
			if (result.status != 200 || result.body.empty())
				continue;

			// The legacy response omits port and protocol; a peer
			// that answered plain HTTP on the probed port serves
			// there, unencrypted.
			ls::LsJson json;
			if (!ls::LsJson::Parse(result.body, json))
				continue;
			ls::LsDevice device;
			if (!ls::LsDevice::FromJson(json, device))
				continue;
			device.port = ls::kDefaultPort;
			device.protocol = ls::kProtocolHttp;
			DeviceAnnounced(device, address, false);
		}
	}
}


void
LocalSendManager::_SendThread(SendArgs* /*args*/)
{
	SendArgs* args = fPendingArgs;
	fPendingArgs = NULL;
	if (args == NULL) {
		fSendLock.Lock();
		fSending = false;
		fSendLock.Unlock();
		return;
	}

	ls::BeHttpTransport transport;
	ls::LsSenderSession session;
	session.SetSender(fDevice);

	int64 totalBytes = 0;
	for (size_t i = 0; i < args->paths.size(); i++) {
		int index = session.AddFile(args->paths[i].String());
		if (index < 0)
			continue;

		// Compute the file checksum so the receiver can verify.
		FILE* file = fopen(args->paths[i].String(), "rb");
		if (file != NULL) {
			ls::LsSha256 sha;
			char buffer[65536];
			size_t n;
			while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
				sha.Update(buffer, n);
			fclose(file);
			char hex[65];
			sha.FinalHex(hex);
			session.FileAt(static_cast<size_t>(index)).item.sha256 = hex;
		}
		totalBytes += session.FileAt(static_cast<size_t>(index)).item.size;
	}

	std::string peer = args->address.String();
	BMessenger messenger = fWindowMessenger;
	ls::LsSenderSession* sessionPtr = &session;
	std::string peerCopy = peer;
	int64 totalAll = totalBytes;
	session.SetProgressCallback([messenger, sessionPtr, peerCopy, totalAll](
		size_t index, int64_t sent, int64_t total) {
		if (!messenger.IsValid())
			return;
		BMessage message(kMsgSendProgress);
		message.AddInt32("index", static_cast<int32>(index));
		message.AddInt64("sent", sent);
		message.AddInt64("total", total);
		message.AddString("peer", peerCopy.c_str());
		if (index < sessionPtr->CountFiles()) {
			message.AddString("file",
				sessionPtr->FileAt(index).item.fileName.c_str());
		}
		// Overall fraction: bytes before this file plus progress
		// inside it, over the whole batch.
		int64 before = 0;
		for (size_t i = 0; i < index && i < sessionPtr->CountFiles(); i++)
			before += sessionPtr->FileAt(i).item.size;
		if (totalAll > 0) {
			float fraction = static_cast<float>(before + sent)
				/ static_cast<float>(totalAll);
			message.AddFloat("fraction", fraction);
		}
		messenger.SendMessage(&message);
	});

	bool success = session.Prepare(&transport, peer, args->device)
		&& session.UploadAll(&transport, peer, args->device);

	LsHistoryEntry entry;
	entry.peerAlias = args->device.alias.empty()
		? args->address : BString(args->device.alias.c_str());
	entry.bytes = totalBytes;
	entry.outgoing = true;
	entry.success = success;
	entry.when = time(NULL);
	std::string names;
	for (size_t i = 0; i < session.CountFiles(); i++) {
		if (i > 0)
			names += ", ";
		names += session.FileAt(i).item.fileName;
	}
	entry.fileNames = names.c_str();
	fSettings->AddHistory(entry);
	fSettings->Save();

	if (messenger.IsValid()) {
		BMessage message(kMsgSendDone);
		message.AddBool("success", success);
		message.AddString("peer", entry.peerAlias);
		message.AddInt32("files", static_cast<int32>(session.CountFiles()));
		message.AddInt64("bytes", totalBytes);
		messenger.SendMessage(&message);
	}

	fSendLock.Lock();
	fSending = false;
	fSendLock.Unlock();
	delete args;
}
