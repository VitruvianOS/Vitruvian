/*
 * Copyright 2015-2018, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef LAUNCH_DAEMON_DEFS_H
#define LAUNCH_DAEMON_DEFS_H


//!	launch_daemon interface


#include <Errors.h>
#include <Roster.h>


namespace BPrivate {


#define kLaunchDaemonSignature "application/x-vnd.Haiku-launch_daemon"
#ifdef TEST_MODE
#	define B_LAUNCH_DAEMON_PORT_NAME "test:launch_daemon"
#else
#	define B_LAUNCH_DAEMON_PORT_NAME "system:launch_daemon"
#endif


// Message constants
enum {
	B_GET_LAUNCH_DATA			= 'lnda',
	B_LAUNCH_TARGET				= 'lntg',
	B_STOP_LAUNCH_TARGET		= 'lnst',
	B_LAUNCH_JOB				= 'lnjo',
	B_ENABLE_LAUNCH_JOB			= 'lnje',
	B_STOP_LAUNCH_JOB			= 'lnsj',
	B_LAUNCH_SESSION			= 'lnse',
	B_REGISTER_SESSION_DAEMON	= 'lnrs',
	B_REGISTER_LAUNCH_EVENT		= 'lnre',
	B_UNREGISTER_LAUNCH_EVENT	= 'lnue',
	B_NOTIFY_LAUNCH_EVENT		= 'lnne',
	B_RESET_STICKY_LAUNCH_EVENT	= 'lnRe',
	B_GET_LAUNCH_TARGETS		= 'lngt',
	B_GET_LAUNCH_JOBS			= 'lngj',
	B_GET_LAUNCH_TARGET_INFO	= 'lntI',
	B_GET_LAUNCH_JOB_INFO		= 'lnjI',
	B_GET_LAUNCH_LOG			= 'lnLL',

	// Janus login broker; sender_uid-gated to vos_login uid.
	B_JANUS_AUTH_REQUEST		= 'jnaR',
	B_JANUS_LOGIN_OK			= 'jnlO',
	// Janus logout: sender_uid-gated to the currently authenticated
	// user's uid; ignored if a system shutdown is already in flight.
	B_JANUS_LOGOUT				= 'jnlX',

	// input_server asks janus to switch VT ("vt" int32); relayed to the
	// current janus_session's control port, since that owns the seat.
	B_JANUS_SWITCH_VT			= 'jnvt',

	// janus_session -> supervisor at startup ("port" string, "uid" int32,
	// "greeter" bool): registers this login's local control port.
	B_JANUS_SESSION_HELLO		= 'jnsH',
	// janus_session -> supervisor per spawned child ("name", "signature"
	// strings, "pid", "port" int32): mirrors it into the app registry.
	B_JANUS_REGISTER_APP		= 'jnrA',
	// janus_session -> supervisor when a child is reaped ("signature"
	// string, "pid" int32): drops that row from the app registry.
	B_JANUS_UNREGISTER_APP		= 'jnuA',
};


}	// namespace BPrivate


#endif	// LAUNCH_DAEMON_DEFS_H

