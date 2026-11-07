/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _TEAM_REF_H
#define _TEAM_REF_H


#include <atomic>

#include <Messenger.h>
#include <OS.h>
#include <SupportDefs.h>


class BMessage;


// Ref counted handle on a team. The count is local to the holding team
// for now; kernel slots that die with the holder land with the nexus job.
class BTeamRef {
public:
								BTeamRef(team_id team,
									status_t* result = NULL);
								BTeamRef(const BMessenger& messenger,
									status_t* result = NULL);
								BTeamRef(const BTeamRef& other);
								~BTeamRef();

			team_id				Team() const;
			const BMessenger&	Messenger() const;
			bool				IsValid() const;

			status_t			Acquire();
			void				Release();

			// Death links ride the existing system watch stream; the
			// target filters with HandlesDeath() per held ref.
			status_t			Watch(BMessenger target);
			status_t			Unwatch(BMessenger target);

			status_t			SendMessage(BMessage* message,
										BMessage* reply = NULL,
										bigtime_t deliveryTimeout
											= B_INFINITE_TIMEOUT,
										bigtime_t replyTimeout
											= B_INFINITE_TIMEOUT) const;

	static	bool				HandlesDeath(const BMessage& message,
										team_id team);

private:
			team_id				fTeam;
			BMessenger			fMessenger;
			std::atomic<int32>	fRefCount;

			int32				_reserved[2];
};

#endif // _TEAM_REF_H
