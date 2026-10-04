/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host test for ModemManagerBackend against a fake ModemManager on a
// private bus. Run under dbus-run-session; the test points the system
// bus address at that session bus before the backend starts.

#include <gio/gio.h>
#include <glib.h>

#include "ModemManagerBackend.h"

#include <Message.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <thread>


static const char* kMMBusName = "org.freedesktop.ModemManager1";
static const char* kMMObjectPath = "/org/freedesktop/ModemManager1";
static const char* kModemPath = "/org/freedesktop/ModemManager1/Modem0";
static const char* kSimPath = "/org/freedesktop/ModemManager1/Sim0";

static const char* kOperatorName = "Vitruvia Mobile";
static const guint32 kSignalQuality = 62;
static const guint32 kUnlockRetries = 3;

// Fake state the test inspects after backend calls.
static char sLastPin[32];
static char sLastPinSimPath[128];
static int sSendPinCount;


static const char* kManagerIntrospection =
	"<node>"
	"  <interface name='org.freedesktop.DBus.ObjectManager'>"
	"    <method name='GetManagedObjects'>"
	"      <arg type='a{oa{sa{sv}}}' name='objects' direction='out'/>"
	"    </method>"
	"  </interface>"
	"  <interface name='org.freedesktop.DBus.Properties'>"
	"    <method name='Ping'/>"
	"  </interface>"
	"</node>";


static const char* kModemIntrospection =
	"<node>"
	"  <interface name='org.freedesktop.DBus.Properties'>"
	"    <method name='Ping'/>"
	"    <method name='Get'>"
	"      <arg type='s' name='interface_name' direction='in'/>"
	"      <arg type='s' name='property_name' direction='in'/>"
	"      <arg type='v' name='value' direction='out'/>"
	"    </method>"
	"    <method name='GetAll'>"
	"      <arg type='s' name='interface_name' direction='in'/>"
	"      <arg type='a{sv}' name='properties' direction='out'/>"
	"    </method>"
	"  </interface>"
	"</node>";


static const char* kSimIntrospection =
	"<node>"
	"  <interface name='org.freedesktop.ModemManager1.Sim'>"
	"    <method name='SendPin'>"
	"      <arg type='s' name='pin' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";


static GVariant*
_UnlockRetriesVariant()
{
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{uu}"));
	g_variant_builder_add(&builder, "{uu}", 2, kUnlockRetries);
	g_variant_builder_add(&builder, "{uu}", 4, kUnlockRetries);
	return g_variant_builder_end(&builder);
}


static GVariant*
_ModemProperty(const char* interfaceName, const char* propertyName)
{
	if (strcmp(interfaceName, "org.freedesktop.ModemManager1.Modem") == 0) {
		if (strcmp(propertyName, "State") == 0)
			return g_variant_new("(i)", 30);
		if (strcmp(propertyName, "EquipmentIdentifier") == 0)
			return g_variant_new_string("FAKE-IMEI-001");
		if (strcmp(propertyName, "Sim") == 0)
			return g_variant_new_object_path(kSimPath);
		if (strcmp(propertyName, "SignalQuality") == 0)
			return g_variant_new("(ub)", kSignalQuality, TRUE);
		if (strcmp(propertyName, "UnlockRequired") == 0)
			return g_variant_new_uint32(2);
		if (strcmp(propertyName, "UnlockRetries") == 0)
			return _UnlockRetriesVariant();
	} else if (strcmp(interfaceName,
			"org.freedesktop.ModemManager1.Modem.Modem3gpp") == 0) {
		if (strcmp(propertyName, "OperatorName") == 0)
			return g_variant_new_string(kOperatorName);
	}
	return NULL;
}


static void
_PropertiesMethodCall(GDBusConnection* connection, const gchar* sender,
	const gchar* objectPath, const gchar* interfaceName,
	const gchar* methodName, GVariant* parameters,
	GDBusMethodInvocation* invocation, gpointer userData)
{
	(void)connection;
	(void)sender;
	(void)objectPath;
	(void)interfaceName;
	(void)userData;

	if (strcmp(methodName, "Ping") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	if (strcmp(methodName, "Get") == 0) {
		const gchar* iface = NULL;
		const gchar* prop = NULL;
		g_variant_get(parameters, "(&s&s)", &iface, &prop);
		GVariant* value = _ModemProperty(iface, prop);
		if (value == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
				"org.freedesktop.DBus.Error.UnknownProperty",
				"unknown property");
			return;
		}
		GVariant* wrapped = g_variant_new_variant(value);
		GVariant* reply = g_variant_new_tuple(&wrapped, 1);
		g_dbus_method_invocation_return_value(invocation, reply);
		return;
	}

	if (strcmp(methodName, "GetAll") == 0) {
		const gchar* iface = NULL;
		g_variant_get(parameters, "(&s)", &iface);
		GVariantBuilder builder;
		g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
		if (strcmp(iface, "org.freedesktop.ModemManager1.Modem") == 0) {
			const gchar* props[] = { "State", "EquipmentIdentifier",
				"Sim", "SignalQuality", "UnlockRequired",
				"UnlockRetries" };
			for (size_t i = 0; i < sizeof(props) / sizeof(props[0]);
					i++) {
				GVariant* v = _ModemProperty(iface, props[i]);
				if (v != NULL)
					g_variant_builder_add(&builder, "{sv}",
						props[i], v);
			}
		} else if (strcmp(iface,
				"org.freedesktop.ModemManager1.Modem.Modem3gpp") == 0) {
			GVariant* v = _ModemProperty(iface, "OperatorName");
			if (v != NULL)
				g_variant_builder_add(&builder, "{sv}",
					"OperatorName", v);
		}
		GVariant* table = g_variant_builder_end(&builder);
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new_tuple(&table, 1));
		return;
	}

	g_dbus_method_invocation_return_dbus_error(invocation,
		"org.freedesktop.DBus.Error.UnknownMethod", methodName);
}


static void
_SimMethodCall(GDBusConnection* connection, const gchar* sender,
	const gchar* objectPath, const gchar* interfaceName,
	const gchar* methodName, GVariant* parameters,
	GDBusMethodInvocation* invocation, gpointer userData)
{
	(void)connection;
	(void)sender;
	(void)interfaceName;
	(void)userData;

	if (strcmp(methodName, "SendPin") == 0) {
		const gchar* pin = NULL;
		g_variant_get(parameters, "(&s)", &pin);
		snprintf(sLastPin, sizeof(sLastPin), "%s", pin != NULL ? pin : "");
		snprintf(sLastPinSimPath, sizeof(sLastPinSimPath), "%s",
			objectPath != NULL ? objectPath : "");
		sSendPinCount++;
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	g_dbus_method_invocation_return_dbus_error(invocation,
		"org.freedesktop.DBus.Error.UnknownMethod", methodName);
}


static void
_ManagerMethodCall(GDBusConnection* connection, const gchar* sender,
	const gchar* objectPath, const gchar* interfaceName,
	const gchar* methodName, GVariant* parameters,
	GDBusMethodInvocation* invocation, gpointer userData)
{
	(void)connection;
	(void)sender;
	(void)objectPath;
	(void)interfaceName;
	(void)parameters;
	(void)userData;

	if (strcmp(methodName, "GetManagedObjects") != 0) {
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.freedesktop.DBus.Error.UnknownMethod", methodName);
		return;
	}

	GVariantBuilder objects;
	g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

	GVariantBuilder modemIfaces;
	g_variant_builder_init(&modemIfaces, G_VARIANT_TYPE("a{sa{sv}}"));

	GVariantBuilder modemProps;
	g_variant_builder_init(&modemProps, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&modemProps, "{sv}", "State",
		g_variant_new("(i)", 30));
	g_variant_builder_add(&modemProps, "{sv}", "EquipmentIdentifier",
		g_variant_new_string("FAKE-IMEI-001"));
	g_variant_builder_add(&modemProps, "{sv}", "Sim",
		g_variant_new_object_path(kSimPath));
	g_variant_builder_add(&modemProps, "{sv}", "SignalQuality",
		g_variant_new("(ub)", kSignalQuality, TRUE));
	g_variant_builder_add(&modemProps, "{sv}", "UnlockRequired",
		g_variant_new_uint32(2));
	g_variant_builder_add(&modemProps, "{sv}", "UnlockRetries",
		_UnlockRetriesVariant());
	g_variant_builder_add(&modemIfaces, "{s@a{sv}}",
		"org.freedesktop.ModemManager1.Modem",
		g_variant_builder_end(&modemProps));

	GVariantBuilder gppProps;
	g_variant_builder_init(&gppProps, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&gppProps, "{sv}", "OperatorName",
		g_variant_new_string(kOperatorName));
	g_variant_builder_add(&modemIfaces, "{s@a{sv}}",
		"org.freedesktop.ModemManager1.Modem.Modem3gpp",
		g_variant_builder_end(&gppProps));

	g_variant_builder_add(&objects, "{o@a{sa{sv}}}", kModemPath,
		g_variant_builder_end(&modemIfaces));

	GVariantBuilder simIfaces;
	g_variant_builder_init(&simIfaces, G_VARIANT_TYPE("a{sa{sv}}"));
	GVariantBuilder simProps;
	g_variant_builder_init(&simProps, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&simIfaces, "{s@a{sv}}",
		"org.freedesktop.ModemManager1.Sim",
		g_variant_builder_end(&simProps));
	g_variant_builder_add(&objects, "{o@a{sa{sv}}}", kSimPath,
		g_variant_builder_end(&simIfaces));

	GVariant* managed = g_variant_builder_end(&objects);
	g_dbus_method_invocation_return_value(invocation,
		g_variant_new_tuple(&managed, 1));
}


static guint sOwnNameId;
static bool sNameOwned;
static GDBusConnection* sExportConnection;
static std::thread sFakeThread;
static GMainLoop* sFakeLoop;
static GMainContext* sFakeContext;
static std::atomic<bool> sFakeReady(false);

static void _OnBusAcquired(GDBusConnection* connection, const gchar* name,
	gpointer userData);
static void _OnNameAcquired(GDBusConnection* connection, const gchar* name,
	gpointer userData);
static void _OnNameLost(GDBusConnection* connection, const gchar* name,
	gpointer userData);
static void _ExportFakeObjects(GDBusConnection* connection);


static void
_FakeThreadMain()
{
	g_main_context_push_thread_default(sFakeContext);
	sFakeLoop = g_main_loop_new(sFakeContext, FALSE);

	GError* error = NULL;
	sExportConnection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (sExportConnection == NULL) {
		fprintf(stderr, "fake: no session bus: %s\n",
			error != NULL ? error->message : "?");
		return;
	}

	sOwnNameId = g_bus_own_name(G_BUS_TYPE_SESSION, kMMBusName,
		G_BUS_NAME_OWNER_FLAGS_NONE, _OnBusAcquired, _OnNameAcquired,
		_OnNameLost, NULL, NULL);

	for (int i = 0; i < 200 && !sNameOwned; i++) {
		g_main_context_iteration(sFakeContext, FALSE);
		usleep(5000);
	}
	if (!sNameOwned) {
		fprintf(stderr, "fake: never acquired %s\n", kMMBusName);
		return;
	}

	_ExportFakeObjects(sExportConnection);
	sFakeReady = true;

	g_main_loop_run(sFakeLoop);
}


static void
_OnBusAcquired(GDBusConnection* connection, const gchar* name,
	gpointer userData)
{
	(void)name;
	(void)userData;
	sExportConnection = connection;
}


static void
_OnNameAcquired(GDBusConnection* connection, const gchar* name,
	gpointer userData)
{
	(void)connection;
	(void)name;
	(void)userData;
	sNameOwned = true;
}


static void
_OnNameLost(GDBusConnection* connection, const gchar* name, gpointer userData)
{
	(void)connection;
	(void)name;
	(void)userData;
	sNameOwned = false;
}


static void
_ExportFakeObjects(GDBusConnection* connection)
{
	GError* error = NULL;

	GDBusNodeInfo* managerInfo = g_dbus_node_info_new_for_xml(
		kManagerIntrospection, &error);
	if (managerInfo == NULL) {
		fprintf(stderr, "manager xml: %s\n",
			error != NULL ? error->message : "?");
		return;
	}
	static const GDBusInterfaceVTable managerVTable = {
		_ManagerMethodCall, NULL, NULL
	};
	g_dbus_connection_register_object(connection,
		kMMObjectPath, managerInfo->interfaces[0], &managerVTable,
		NULL, NULL, &error);
	if (error != NULL) {
		g_error_free(error);
		error = NULL;
	}

	static const GDBusInterfaceVTable managerPropsVTable = {
		_PropertiesMethodCall, NULL, NULL
	};
	if (managerInfo->interfaces[1] != NULL) {
		g_dbus_connection_register_object(connection,
			kMMObjectPath, managerInfo->interfaces[1],
			&managerPropsVTable, NULL, NULL, &error);
		if (error != NULL) {
			g_error_free(error);
			error = NULL;
		}
	}
	g_dbus_node_info_unref(managerInfo);

	GDBusNodeInfo* modemInfo = g_dbus_node_info_new_for_xml(
		kModemIntrospection, &error);
	if (modemInfo == NULL) {
		fprintf(stderr, "modem xml: %s\n",
			error != NULL ? error->message : "?");
		return;
	}
	static const GDBusInterfaceVTable modemVTable = {
		_PropertiesMethodCall, NULL, NULL
	};
	for (int i = 0; modemInfo->interfaces[i] != NULL; i++) {
		const char* ifaceName = modemInfo->interfaces[i]->name;
		guint id = g_dbus_connection_register_object(connection,
			kModemPath, modemInfo->interfaces[i], &modemVTable,
			NULL, NULL, &error);
		(void)ifaceName;
		(void)id;
		if (error != NULL) {
			g_error_free(error);
			error = NULL;
		}
	}
	g_dbus_node_info_unref(modemInfo);

	GDBusNodeInfo* simInfo = g_dbus_node_info_new_for_xml(
		kSimIntrospection, &error);
	if (simInfo == NULL) {
		fprintf(stderr, "sim xml: %s\n",
			error != NULL ? error->message : "?");
		return;
	}
	static const GDBusInterfaceVTable simVTable = {
		_SimMethodCall, NULL, NULL
	};
	guint simId = g_dbus_connection_register_object(connection, kSimPath,
		simInfo->interfaces[0], &simVTable, NULL, NULL, &error);
	(void)simId;
	g_dbus_node_info_unref(simInfo);

	if (error != NULL)
		g_error_free(error);
}


static int sFailures;


static void
_Check(bool ok, const char* label)
{
	printf("%-48s %s\n", label, ok ? "OK" : "FAIL");
	if (!ok)
		sFailures++;
}


static bool
_WaitForName(GDBusConnection* bus)
{
	for (int i = 0; i < 100; i++) {
		while (g_main_context_iteration(NULL, FALSE))
			;

		GError* error = NULL;
		GVariant* probe = g_dbus_connection_call_sync(bus, kMMBusName,
			kMMObjectPath, "org.freedesktop.DBus.ObjectManager",
			"GetManagedObjects", NULL,
			G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
			G_DBUS_CALL_FLAGS_NONE, 500, NULL, &error);
		if (probe != NULL) {
			g_variant_unref(probe);
			return true;
		}
		if (error != NULL)
			g_error_free(error);
		usleep(10000);
	}
	return sNameOwned;
}


static bool
_WaitForSnapshot(ModemManagerBackend* backend, BMessage& modems)
{
	for (int i = 0; i < 50; i++) {
		while (g_main_context_iteration(NULL, FALSE))
			;

		if (backend->GetModems(&modems) != B_OK)
			return false;
		bool available = false;
		int32 count = 0;
		modems.FindBool(kMMFieldMMAvailable, &available);
		modems.FindInt32(kMMFieldModemCount, &count);
		if (available && count > 0)
			return true;
		usleep(20000);
	}
	return false;
}


int
main()
{
	const char* session = getenv("DBUS_SESSION_BUS_ADDRESS");
	if (session == NULL || session[0] == '\0') {
		fprintf(stderr, "run this test under dbus-run-session\n");
		return 77;
	}
	// Backend hardcodes the system bus; point it at the private bus.
	setenv("DBUS_SYSTEM_BUS_ADDRESS", session, 1);

	GError* error = NULL;
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (bus == NULL) {
		fprintf(stderr, "no session bus: %s\n",
			error != NULL ? error->message : "unknown");
		return 77;
	}

	sFakeContext = g_main_context_new();
	sFakeThread = std::thread(_FakeThreadMain);
	for (int i = 0; i < 200 && !sFakeReady; i++)
		usleep(10000);
	if (!sFakeReady) {
		fprintf(stderr, "fake ModemManager never became ready\n");
		return 77;
	}

	if (!_WaitForName(bus)) {
		fprintf(stderr, "fake ModemManager never owned %s\n", kMMBusName);
		return 77;
	}

	ModemManagerBackend* backend = ModemManagerBackend::Instance();

	BMessage modems;
	bool snap = _WaitForSnapshot(backend, modems);
	_Check(snap, "snapshot has a modem");
	if (!snap) {
		printf("\n%d failure(s)\n", sFailures);
		return sFailures == 0 ? 0 : 1;
	}

	const char* key = "modem_0";
	BMessage info;
	if (modems.FindMessage(key, &info) != B_OK) {
		printf("no modem_0 in list\n");
		printf("\n%d failure(s)\n", sFailures);
		return 1;
	}

	const char* operatorName = NULL;
	info.FindString(kMMFieldModemOperatorName, &operatorName);
	_Check(operatorName != NULL
		&& strcmp(operatorName, kOperatorName) == 0,
		"operator name from Modem.Modem3gpp");

	uint32 quality = 0;
	info.FindUInt32(kMMFieldModemSignalQuality, &quality);
	_Check(quality == kSignalQuality, "signal quality from Modem");

	const char* modemPath = NULL;
	info.FindString(kMMFieldModemPath, &modemPath);
	if (modemPath == NULL)
		modemPath = kModemPath;

	BMessage liveSignal;
	status_t sigStatus = backend->GetSignalQuality(modemPath, &liveSignal);
	uint32 liveQuality = 0;
	liveSignal.FindUInt32(kMMFieldModemSignalQuality, &liveQuality);
	_Check(sigStatus == B_OK && liveQuality == kSignalQuality,
		"GetSignalQuality reads Modem.SignalQuality");

	bool locked = false;
	bool pinRequired = false;
	uint32 retries = 0;
	info.FindBool(kMMFieldSimLocked, &locked);
	info.FindBool(kMMFieldSimPinRequired, &pinRequired);
	info.FindUInt32(kMMFieldSimUnlockRetries, &retries);
	_Check(locked && pinRequired, "snapshot reports SIM PIN locked");
	_Check(retries == kUnlockRetries, "snapshot reports PIN retries");

	BMessage simInfo;
	status_t simStatus = backend->GetSimStatus(modemPath, &simInfo);
	bool simLocked = false;
	bool simPin = false;
	uint32 simRetries = 0;
	simInfo.FindBool(kMMFieldSimLocked, &simLocked);
	simInfo.FindBool(kMMFieldSimPinRequired, &simPin);
	simInfo.FindUInt32(kMMFieldSimUnlockRetries, &simRetries);
	const char* simReason = NULL;
	simInfo.FindString("reason", &simReason);
	(void)simReason;
	_Check(simStatus == B_OK && simLocked && simPin,
		"GetSimStatus reports PIN lock");
	_Check(simRetries == kUnlockRetries, "GetSimStatus reports retries");

	sLastPin[0] = '\0';
	sLastPinSimPath[0] = '\0';
	sSendPinCount = 0;
	status_t unlock = backend->UnlockSim(modemPath, "1234");
	_Check(unlock == B_OK, "UnlockSim returns B_OK");
	_Check(sSendPinCount == 1, "SendPin reached the fake once");
	_Check(strcmp(sLastPin, "1234") == 0, "SendPin carried the PIN");
	_Check(strcmp(sLastPinSimPath, kSimPath) == 0,
		"SendPin was addressed to the Sim object");

	if (sFakeLoop != NULL)
		g_main_loop_quit(sFakeLoop);
	if (sFakeThread.joinable())
		sFakeThread.join();
	if (sOwnNameId != 0)
		g_bus_unown_name(sOwnNameId);
	g_object_unref(bus);

	printf("\n%d failure(s)\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
