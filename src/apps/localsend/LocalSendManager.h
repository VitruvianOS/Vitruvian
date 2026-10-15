/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LOCAL_SEND_MANAGER_H
#define _LOCAL_SEND_MANAGER_H

#include <Locker.h>
#include <Messenger.h>
#include <String.h>

#include <map>
#include <string>
#include <vector>

#include "LocalSendSettings.h"
#include "LsDiscovery.h"
#include "LsHttpServer.h"
#include "LsReceiverSession.h"

namespace LocalSend {
	class LsSenderSession;
}

class LocalSendManager : public LocalSend::LsDiscoveryListener,
	public LocalSend::LsReceivePrompt {
public:
						LocalSendManager(LocalSendSettings* settings);
						~LocalSendManager();

			status_t	Start();
			void		Stop();

			void		SetWindowMessenger(const BMessenger& messenger)
								{ fWindowMessenger = messenger; }

			// Sends |paths| to |device| at |address|. Runs on a
			// worker thread; progress and completion come back as
			// messages to the window messenger.
			void		SendFiles(const std::vector<BString>& paths,
								const BString& address,
								const LocalSend::LsDevice& device);

			bool		IsSending() const { fSendLock.Lock();
								bool sending = fSending;
								fSendLock.Unlock();
								return sending; }

			// LocalSend::LsDiscoveryListener
	virtual	void		DeviceAnnounced(const LocalSend::LsDevice& device,
								const std::string& sourceAddress,
								bool announceFlag);

			// LocalSend::LsReceivePrompt: called on a server worker
			// thread; blocks until the UI answers or times out.
	virtual	LocalSend::LsReceiveDecision	Prompt(
								const LocalSend::LsDevice& sender,
								const std::map<std::string,
									LocalSend::LsFileItem>& files,
								const std::string& pin,
								std::vector<std::string>& acceptedFiles);

			// Called by the prompt window on the UI thread.
			void		AnswerPrompt(bool accept, bool alwaysAllow,
								const std::vector<BString>& fileNames);

			// True while an accept prompt is waiting for an answer.
			bool		HasPendingPrompt() const
								{ return fPromptPending; }

			LocalSend::LsDevice		OurDevice() const { return fDevice; }
			uint16_t	ServerPort() const;

private:
			struct SendArgs {
				std::vector<BString>	paths;
				BString					address;
				LocalSend::LsDevice		device;
			};

			static	int32_t	_SendThreadEntry(void* data);
			void		_SendThread(SendArgs* args);

			static	int32_t	_ScanThreadEntry(void* data);
			void		_ScanSubnets();

			LocalSendSettings*	fSettings;
			mutable BLocker		fSendLock;
			BMessenger	fWindowMessenger;

			LocalSend::LsDiscovery	fDiscovery;
			LocalSend::LsHttpServer	fServer;
			LocalSend::LsReceiverSession	fReceiver;
			LocalSend::LsDevice	fDevice;

			bool		fSending;
			SendArgs*	fPendingArgs;

			// Pending prompt handshake between server thread and UI.
			BLocker		fPromptLock;
			sem_id		fPromptSemaphore;
			volatile bool	fPromptPending;
			bool		fPromptAccept;
			bool		fPromptAlwaysAllow;
			std::vector<std::string>	fPromptAcceptedFiles;
			LocalSend::LsDevice	fPromptSender;
};

#endif // _LOCAL_SEND_MANAGER_H
