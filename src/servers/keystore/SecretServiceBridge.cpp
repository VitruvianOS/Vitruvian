/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SecretServiceBridge.h"

#include "SecretServiceMap.h"

#include <KeyStore.h>
#include <String.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <gio/gio.h>


static const char* kSecretServiceName = "org.freedesktop.secrets";
static const char* kSecretServicePath = "/org/freedesktop/secrets";
static const char* kDefaultCollectionPath
	= "/org/freedesktop/secrets/aliases/default";


static const gchar kServiceXml[] =
	"<node>"
	"  <interface name='org.freedesktop.secrets.Service'>"
	"    <method name='OpenSession'>"
	"      <arg name='algorithm' type='s' direction='in'/>"
	"      <arg name='input' type='ay' direction='in'/>"
	"      <arg name='output' type='ay' direction='out'/>"
	"      <arg name='result' type='o' direction='out'/>"
	"    </method>"
	"    <method name='SearchItems'>"
	"      <arg name='attributes' type='a{ss}' direction='in'/>"
	"      <arg name='unlocked' type='ao' direction='out'/>"
	"      <arg name='locked' type='ao' direction='out'/>"
	"    </method>"
	"    <method name='GetSecrets'>"
	"      <arg name='items' type='ao' direction='in'/>"
	"      <arg name='session' type='o' direction='in'/>"
	"      <arg name='secrets' type='a{o(oayays)}' direction='out'/>"
	"    </method>"
	"    <method name='Unlock'>"
	"      <arg name='objects' type='ao' direction='in'/>"
	"      <arg name='unlocked' type='ao' direction='out'/>"
	"      <arg name='prompts' type='ao' direction='out'/>"
	"    </method>"
	"    <method name='ReadAlias'>"
	"      <arg name='name' type='s' direction='in'/>"
	"      <arg name='collection' type='o' direction='out'/>"
	"    </method>"
	"    <method name='SetAlias'>"
	"      <arg name='name' type='s' direction='in'/>"
	"      <arg name='value' type='o' direction='in'/>"
	"    </method>"
	"    <property name='Collections' type='ao' access='read'/>"
	"  </interface>"
	"</node>";


class SecretServiceBridge::Impl {
public:
								Impl();
								~Impl();

			status_t			Start();
			void				Stop();

			void				HandleMethodCall(GDBusMethodInvocation* invocation,
									const gchar* method, GVariant* parameters);

			static void*		_ThreadEntry(void* cookie);
			void				_ThreadLoop();

private:
			void				_ReplyOpenSession(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplySearchItems(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyGetSecrets(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyUnlock(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyReadAlias(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplySetAlias(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyCollections(GDBusMethodInvocation* invocation);

			bool				_ParseItemPath(const char* path, BString& keyring,
									BString& identifier, BString& username);

			GDBusConnection*	fConnection;
			GMainContext*		fContext;
			GMainLoop*			fLoop;
			pthread_t			fThread;
			bool				fRunning;
			guint				fRegistrationId;
			BKeyStore			fKeyStore;
};


SecretServiceBridge::Impl::Impl()
	:
	fConnection(NULL),
	fContext(NULL),
	fLoop(NULL),
	fRunning(false),
	fRegistrationId(0)
{
}


SecretServiceBridge::Impl::~Impl()
{
	Stop();
}


static void
_on_method_call(GDBusConnection* connection, const gchar* sender,
	const gchar* objectPath, const gchar* interfaceName,
	const gchar* methodName, GVariant* parameters,
	GDBusMethodInvocation* invocation, gpointer user_data)
{
	SecretServiceBridge::Impl* impl = (SecretServiceBridge::Impl*)user_data;
	impl->HandleMethodCall(invocation, methodName, parameters);
}


void
SecretServiceBridge::Impl::HandleMethodCall(GDBusMethodInvocation* invocation,
	const gchar* method, GVariant* parameters)
{
	if (strcmp(method, "OpenSession") == 0)
		_ReplyOpenSession(invocation, parameters);
	else if (strcmp(method, "SearchItems") == 0)
		_ReplySearchItems(invocation, parameters);
	else if (strcmp(method, "GetSecrets") == 0)
		_ReplyGetSecrets(invocation, parameters);
	else if (strcmp(method, "Unlock") == 0)
		_ReplyUnlock(invocation, parameters);
	else if (strcmp(method, "ReadAlias") == 0)
		_ReplyReadAlias(invocation, parameters);
	else if (strcmp(method, "SetAlias") == 0)
		_ReplySetAlias(invocation, parameters);
	else if (strcmp(method, "Get") == 0)
		_ReplyCollections(invocation);
	else
		g_dbus_method_invocation_return_error(invocation,
			G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Method %s is not implemented", method);
}


void
SecretServiceBridge::Impl::_ReplyOpenSession(GDBusMethodInvocation* invocation,
	GVariant* parameters)
{
	gchar* algorithm = NULL;
	GVariant* input = NULL;
	g_variant_get(parameters, "(&s@ay)", &algorithm, &input);

	if (strcmp(algorithm, "plain") != 0) {
		g_dbus_method_invocation_return_error(invocation,
			G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Only the plain session algorithm is supported");
		g_free(algorithm);
		g_variant_unref(input);
		return;
	}

	GVariant* output = g_variant_ref_sink(g_variant_new_array(
		G_VARIANT_TYPE_BYTE, NULL, 0));
	g_variant_unref(input);
	g_free(algorithm);

	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(vo)", output, kSecretServicePath));
}


void
SecretServiceBridge::Impl::_ReplySearchItems(
	GDBusMethodInvocation* invocation, GVariant* parameters)
{
	SsAttributeSet query;
	memset(&query, 0, sizeof(query));

	GVariantIter* iter = NULL;
	g_variant_get(parameters, "(a{ss})", &iter);
	gchar* key = NULL;
	gchar* value = NULL;
	while (g_variant_iter_next(iter, "{ss}", &key, &value)) {
		ss_AttributeSetAdd(&query, key, value);
		g_free(key);
		g_free(value);
	}
	g_variant_iter_free(iter);

	GVariantBuilder unlocked;
	GVariantBuilder locked;
	g_variant_builder_init(&unlocked, G_VARIANT_TYPE("ao"));
	g_variant_builder_init(&locked, G_VARIANT_TYPE("ao"));

	uint32 cookie = 0;
	BPasswordKey password;
	while (true) {
		status_t result = fKeyStore.GetNextKey(NULL, B_KEY_TYPE_ANY,
			B_KEY_PURPOSE_ANY, cookie, password);
		if (result == B_ENTRY_NOT_FOUND)
			break;
		if (result != B_OK)
			break;

		SsAttributeSet stored;
		memset(&stored, 0, sizeof(SsAttributeSet));
		ss_AttributeSetAdd(&stored, "key", password.Identifier());
		ss_AttributeSetAdd(&stored, "user", password.SecondaryIdentifier());
		ss_AttributeSetAdd(&stored, "xdg:schema",
			"org.freedesktop.Secret.Service");

		if (!ss_AttributesMatch(&query, &stored))
			continue;

		char path[256];
		if (ss_ItemPath("default", password.Identifier(),
				password.SecondaryIdentifier(), path, sizeof(path)) == 0) {
			if (fKeyStore.IsKeyringUnlocked("default"))
				g_variant_builder_add(&unlocked, "o", path);
			else
				g_variant_builder_add(&locked, "o", path);
		}
	}

	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(@ao@ao)",
			g_variant_builder_end(&unlocked),
			g_variant_builder_end(&locked)));
}


void
SecretServiceBridge::Impl::_ReplyGetSecrets(
	GDBusMethodInvocation* invocation, GVariant* parameters)
{
	GVariantIter* iter = NULL;
	const gchar* session = NULL;
	g_variant_get(parameters, "(^ao&o)", &iter, &session);

	GVariantBuilder secrets;
	g_variant_builder_init(&secrets, G_VARIANT_TYPE("a{o(oayays)}"));

	gchar* itemPath = NULL;
	while (g_variant_iter_next(iter, "o", &itemPath)) {
		BString keyring;
		BString identifier;
		BString username;
		if (_ParseItemPath(itemPath, keyring, identifier, username)) {
			BPasswordKey key;
			if (fKeyStore.GetKey(keyring.String(), B_KEY_TYPE_PASSWORD,
					identifier.String(), username.String(), key) == B_OK
				&& key.DataLength() > 0) {
				GVariant* value = g_variant_new_fixed_array(
					G_VARIANT_TYPE_BYTE, key.Data(), key.DataLength(), 1);
				GVariant* params = g_variant_new_array(G_VARIANT_TYPE_BYTE,
					NULL, 0);
				GVariant* secret = g_variant_new("(o@ay@ays)",
					kSecretServicePath, params, value,
					"text/plain");
				g_variant_builder_add(&secrets, "{o@(oayays)}",
					itemPath, secret);
			}
		}
		g_free(itemPath);
		itemPath = NULL;
	}
	g_variant_iter_free(iter);

	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(@a{o(oayays)})", g_variant_builder_end(&secrets)));
}


void
SecretServiceBridge::Impl::_ReplyUnlock(GDBusMethodInvocation* invocation,
	GVariant* parameters)
{
	GVariantIter* iter = NULL;
	g_variant_get(parameters, "(^ao)", &iter);

	GVariantBuilder unlocked;
	GVariantBuilder prompts;
	g_variant_builder_init(&unlocked, G_VARIANT_TYPE("ao"));
	g_variant_builder_init(&prompts, G_VARIANT_TYPE("ao"));

	gchar* path = NULL;
	while (g_variant_iter_next(iter, "o", &path)) {
		BString keyring;
		BString identifier;
		BString username;
		if (_ParseItemPath(path, keyring, identifier, username)
			&& fKeyStore.IsKeyringUnlocked(keyring.String()))
			g_variant_builder_add(&unlocked, "o", path);
		else
			g_variant_builder_add(&prompts, "o", path);
		g_free(path);
		path = NULL;
	}
	g_variant_iter_free(iter);

	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(@ao@ao)",
			g_variant_builder_end(&unlocked),
			g_variant_builder_end(&prompts)));
}


void
SecretServiceBridge::Impl::_ReplyReadAlias(GDBusMethodInvocation* invocation,
	GVariant* parameters)
{
	const gchar* name = NULL;
	g_variant_get(parameters, "(&s)", &name);
	if (strcmp(name, "default") == 0)
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(o)", kDefaultCollectionPath));
	else
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(o)", "/"));
}


void
SecretServiceBridge::Impl::_ReplySetAlias(GDBusMethodInvocation* invocation,
	GVariant* parameters)
{
	g_dbus_method_invocation_return_value(invocation, NULL);
}


void
SecretServiceBridge::Impl::_ReplyCollections(GDBusMethodInvocation* invocation)
{
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
	g_variant_builder_add(&builder, "o", kDefaultCollectionPath);
	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(v)", g_variant_builder_end(&builder)));
}


bool
SecretServiceBridge::Impl::_ParseItemPath(const char* path, BString& keyring,
	BString& identifier, BString& username)
{
	static const char* kPrefix = "/org/freedesktop/secrets/collection/";
	if (path == NULL || strncmp(path, kPrefix, strlen(kPrefix)) != 0)
		return false;

	const char* rest = path + strlen(kPrefix);
	const char* slash = strchr(rest, '/');
	if (slash == NULL) {
		keyring.SetTo(rest);
		return false;
	}

	keyring.SetTo(rest, (size_t)(slash - rest));
	const char* leaf = slash + 1;
	const char* underscore = strrchr(leaf, '_');
	if (underscore != NULL) {
		identifier.SetTo(leaf, (size_t)(underscore - leaf));
		username.SetTo(underscore + 1);
	} else {
		identifier.SetTo(leaf);
		username = "";
	}
	return identifier.Length() > 0;
}


void*
SecretServiceBridge::Impl::_ThreadEntry(void* cookie)
{
	((SecretServiceBridge::Impl*)cookie)->_ThreadLoop();
	return NULL;
}


void
SecretServiceBridge::Impl::_ThreadLoop()
{
	fContext = g_main_context_new();
	g_main_context_push_thread_default(fContext);
	fLoop = g_main_loop_new(fContext, FALSE);

	GError* error = NULL;
	fConnection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (fConnection == NULL) {
		fprintf(stderr, "SecretServiceBridge: session bus unavailable: %s\n",
			error != NULL ? error->message : "unknown");
		if (error != NULL)
			g_error_free(error);
		g_main_loop_unref(fLoop);
		fLoop = NULL;
		g_main_context_pop_thread_default(fContext);
		g_main_context_unref(fContext);
		fContext = NULL;
		return;
	}

	GDBusNodeInfo* nodeInfo = g_dbus_node_info_new_for_xml(kServiceXml,
		&error);
	if (nodeInfo == NULL) {
		fprintf(stderr, "SecretServiceBridge: bad interface XML\n");
		g_object_unref(fConnection);
		fConnection = NULL;
		return;
	}

	static const GDBusInterfaceVTable kVTable = {
		_on_method_call, NULL, NULL, { 0 }
	};

	fRegistrationId = g_dbus_connection_register_object(fConnection,
		kSecretServicePath, nodeInfo->interfaces[0], &kVTable, this, NULL,
		&error);
	if (fRegistrationId == 0) {
		fprintf(stderr, "SecretServiceBridge: register failed: %s\n",
			error != NULL ? error->message : "unknown");
		if (error != NULL)
			g_error_free(error);
		g_dbus_node_info_unref(nodeInfo);
		g_object_unref(fConnection);
		fConnection = NULL;
		return;
	}

	GDBusProxy* proxy = g_dbus_proxy_new_sync(fConnection,
		G_DBUS_PROXY_FLAGS_NONE, NULL, "org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", NULL, &error);
	if (proxy != NULL) {
		GVariant* result = g_dbus_proxy_call_sync(proxy, "RequestName",
			g_variant_new("(su)", kSecretServiceName, 0),
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		if (result != NULL)
			g_variant_unref(result);
		else if (error != NULL) {
			fprintf(stderr, "SecretServiceBridge: RequestName: %s\n",
				error->message);
			g_error_free(error);
			error = NULL;
		}
		g_object_unref(proxy);
	}

	g_main_loop_run(fLoop);

	if (fRegistrationId != 0)
		g_dbus_connection_unregister_object(fConnection, fRegistrationId);
	g_dbus_node_info_unref(nodeInfo);
	g_object_unref(fConnection);
	fConnection = NULL;
	g_main_loop_unref(fLoop);
	fLoop = NULL;
	g_main_context_pop_thread_default(fContext);
	g_main_context_unref(fContext);
	fContext = NULL;
}


status_t
SecretServiceBridge::Impl::Start()
{
	if (fRunning)
		return B_OK;

	fRunning = true;
	if (pthread_create(&fThread, NULL, &_ThreadEntry, this) != 0) {
		fRunning = false;
		return B_ERROR;
	}
	return B_OK;
}


void
SecretServiceBridge::Impl::Stop()
{
	if (!fRunning)
		return;

	if (fLoop != NULL)
		g_main_loop_quit(fLoop);
	pthread_join(fThread, NULL);
	fRunning = false;
}


SecretServiceBridge::SecretServiceBridge()
	:
	fImpl(new Impl())
{
}


SecretServiceBridge::~SecretServiceBridge()
{
	delete fImpl;
}


status_t
SecretServiceBridge::Start()
{
	return fImpl->Start();
}


void
SecretServiceBridge::Stop()
{
	fImpl->Stop();
}
