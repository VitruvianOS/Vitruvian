/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef OUTPUT_ADDON_MANAGER_H
#define OUTPUT_ADDON_MANAGER_H


#include <Locker.h>
#include <Looper.h>
#include <ObjectList.h>
#include <Rect.h>
#include <Region.h>
#include <String.h>

#include "HWInterface.h"

#include <OutputAddonProtocol.h>


class AppServer;
class BMessage;


// Supervises out-of-process output add-ons: registration over the
// well-known port named OUTPUT_ADDON_PORT_NAME, per-output capture
// subscriptions with the shared damage transport, input forwarding and
// injection, virtual outputs, and crash isolation. A crashed add-on
// loses only its own team; app_server drops its subscriptions through
// the team-deletion watch.
class OutputAddonManager : public BLooper, public HWInterfaceListener {
public:
								OutputAddonManager(AppServer* appServer);
	virtual						~OutputAddonManager();

			status_t			InitCheck();

	virtual	void				MessageReceived(BMessage* message);

			// Capture hook: the interface is write-locked by the caller,
			// listeners must not lock it again.
	virtual	void				FrameCommitted(HWInterface* interface,
									const BRect& frame);

			// From the event dispatcher thread, one stream event; no-op
			// unless an add-on asked for input.
			void				PublishInput(const BMessage& event);

private:
			struct subscription {
				int32			team;
				int32			screen;
				HWInterface*	interface;
				uint32			transport;
				port_id			updatePort;
				port_id			eventPort;
				area_id			area;
				output_addon_fb_header* header;
				int32			bytesPerRow;
				int32			width;
				int32			height;
				uint32			generation;
				BRegion			pending;
			};

			struct addon_team {
				int32			team;
				int32			connections;
				bool			spawned;
				bool			respawn;
				BString			path;
			};

			void				_HandleRegister(BMessage* message);
			void				_HandleQuit(BMessage* message);
			void				_HandleCreateVirtual(BMessage* message);
			void				_HandleDeleteVirtual(BMessage* message);
			void				_HandleAttach(BMessage* message);
			void				_HandleDetach(BMessage* message);
			void				_HandleInjectInput(BMessage* message);
			void				_HandleLaunch(BMessage* message);
			void				_HandleTeamDeleted(team_id team);

			addon_team*		_TeamEntry(int32 team, bool create);
			subscription*		_FindSubscription(int32 team, int32 screen);
			void				_RemoveSubscriptions(int32 team);
			void				_DetachLocked(subscription* subscription);
			void				_PublishLocked(subscription* subscription);
			bool				_SenderAllowed(BMessage* message) const;
			void				_SendReply(BMessage* message, status_t status);

			AppServer*			fAppServer;
			status_t			fInitStatus;
			volatile int32		fCaptureSubscribers;
			volatile int32		fInputSubscribers;
			BLocker				fLock;
			BObjectList<subscription, true>
								fSubscriptions;
			BObjectList<addon_team, true>
								fTeams;
};


extern OutputAddonManager* gOutputAddonManager;


#endif	// OUTPUT_ADDON_MANAGER_H
