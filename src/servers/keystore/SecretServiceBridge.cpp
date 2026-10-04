/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SecretServiceBridge.h"

#include "SecretServiceMap.h"

#include <KeyStore.h>
#include <ObjectList.h>
#include <String.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <gio/gio.h>


static const char* kSecretServiceName = "org.freedesktop.secrets";
static const char* kSecretServicePath = "/org/freedesktop/secrets";
static const char* kCollectionPrefix = "/org/freedesktop/secrets/collection/";
static const char* kDefaultAlias = "default";
static const char* kMasterKeyringName = "Master";


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
	"  <interface name='org.freedesktop.secrets.Collection'>"
	"    <method name='SearchItems'>"
	"      <arg name='attributes' type='a{ss}' direction='in'/>"
	"      <arg name='unlocked' type='ao' direction='out'/>"
	"      <arg name='locked' type='ao' direction='out'/>"
	"    </method>"
	"    <method name='Delete'>"
	"      <arg name='prompt' type='o' direction='out'/>"
	"    </method>"
	"    <property name='Items' type='ao' access='read'/>"
	"    <property name='Locked' type='b' access='read'/>"
	"    <property name='Label' type='s' access='read'/>"
	"    <property name='Created' type='t' access='read'/>"
	"    <property name='Modified' type='t' access='read'/>"
	"  </interface>"
	"</node>";


class SecretServiceBridge::Impl {
public:
								Impl();
								~Impl();

			status_t			Start();
			void				Stop();

			void				HandleMethodCall(GDBusMethodInvocation* invocation,
									const gchar* objectPath,
									const gchar* method, GVariant* parameters);
			GVariant*			HandleGetProperty(const gchar* objectPath,
									const gchar* interfaceName,
									const gchar* propertyName);

			static void*		_ThreadEntry(void* cookie);
			void				_ThreadLoop();

private:
			struct Collection {
				BString			keyring;
				BString			path;
			};

			void				_ReplyOpenSession(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplySearchItems(GDBusMethodInvocation* invocation,
									GVariant* parameters, const BString& keyring);
			void				_ReplyGetSecrets(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyUnlock(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyReadAlias(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplySetAlias(GDBusMethodInvocation* invocation,
									GVariant* parameters);
			void				_ReplyCollections(GDBusMethodInvocation* invocation);
			void				_ReplyCollectionItems(
									GDBusMethodInvocation* invocation,
									const BString& keyring);
			void				_ReplyCollectionLocked(
									GDBusMethodInvocation* invocation,
									const BString& keyring);
			void				_ReplyCollectionLabel(
									GDBusMethodInvocation* invocation,
									const BString& keyring);

			bool				_LoadCollections(BObjectList<Collection>& collections);
			bool				_KeyringForPath(const char* path, BString& keyring,
									bool* _isCollection);
			bool				_UnlockKeyring(const BString& keyring);
			void				_SearchKeyring(const BString& keyring,
									const SsAttributeSet& query,
									GVariantBuilder* unlocked,
									GVariantBuilder* locked);

			GDBusConnection*	fConnection;
			GMainContext*		fContext;
			GMainLoop*			fLoop;
			pthread_t			fThread;
			bool				fRunning;
			guint				fRegistrationId;
			BObjectList<Collection> fRegistrations;
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
	impl->HandleMethodCall(invocation, objectPath, methodName, parameters);
}


static GVariant*
_on_get_property(GDBusConnection* connection, const gchar* sender,
	const gchar* objectPath, const gchar* interfaceName,
	const gchar* propertyName, GError** error, gpointer user_data)
{
	SecretServiceBridge::Impl* impl = (SecretServiceBridge::Impl*)user_data;
	return impl->HandleGetProperty(objectPath, interfaceName, propertyName);
}


void
SecretServiceBridge::Impl::HandleMethodCall(GDBusMethodInvocation* invocation,
	const gchar* objectPath, const gchar* method, GVariant* parameters)
{
	bool onCollection = objectPath != NULL
		&& strncmp(objectPath, kCollectionPrefix, strlen(kCollectionPrefix))
			== 0;

	if (strcmp(method, "OpenSession") == 0)
		_ReplyOpenSession(invocation, parameters);
	else if (strcmp(method, "SearchItems") == 0) {
		BString keyring;
		if (onCollection)
			_KeyringForPath(objectPath, keyring, NULL);
		_ReplySearchItems(invocation, parameters, keyring);
	} else if (strcmp(method, "GetSecrets") == 0)
		_ReplyGetSecrets(invocation, parameters);
	else if (strcmp(method, "Unlock") == 0)
		_ReplyUnlock(invocation, parameters);
	else if (strcmp(method, "ReadAlias") == 0)
		_ReplyReadAlias(invocation, parameters);
	else if (strcmp(method, "SetAlias") == 0)
		_ReplySetAlias(invocation, parameters);
	else if (strcmp(method, "Delete") == 0) {
		g_dbus_method_invocation_return_error(invocation,
			G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Deleting collections is not implemented");
	} else if (strcmp(method, "Get") == 0)
		_ReplyCollections(invocation);
	else
		g_dbus_method_invocation_return_error(invocation,
			G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			"Method %s is not implemented", method);
}


GVariant*
SecretServiceBridge::Impl::HandleGetProperty(const gchar* objectPath,
	const gchar* interfaceName, const gchar* propertyName)
{
	if (objectPath == NULL)
		return NULL;

	if (strcmp(interfaceName, "org.freedesktop.secrets.Service") == 0
		&& strcmp(propertyName, "Collections") == 0) {
		GVariantBuilder builder;
		g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
		BObjectList<Collection> collections;
		if (_LoadCollections(collections)) {
			for (int32 i = 0; i < collections.CountItems(); i++) {
				Collection* collection = collections.ItemAt(i);
				g_variant_builder_add(&builder, "o", collection->path.String());
			}
		}
		return g_variant_ref_sink(g_variant_builder_end(&builder));
	}

	bool isCollection = false;
	BString keyring;
	if (!_KeyringForPath(objectPath, keyring, &isCollection) || !isCollection)
		return NULL;

	if (strcmp(interfaceName, "org.freedesktop.secrets.Collection") != 0)
		return NULL;

	if (strcmp(propertyName, "Items") == 0) {
		SsAttributeSet query;
		memset(&query, 0, sizeof(query));
		ss_AttributeSetAdd(&query, "any", "item");
		GVariantBuilder unlocked;
		GVariantBuilder locked;
		g_variant_builder_init(&unlocked, G_VARIANT_TYPE("ao"));
		g_variant_builder_init(&locked, G_VARIANT_TYPE("ao"));
		_SearchKeyring(keyring, query, &unlocked, &locked);
		GVariantBuilder all;
		g_variant_builder_init(&all, G_VARIANT_TYPE("ao"));
		GVariantIter iter;
		g_variant_iter_init(&iter, g_variant_builder_end(&unlocked));
		const gchar* path = NULL;
		while (g_variant_iter_next(&iter, "&o", &path))
			g_variant_builder_add(&all, "o", path);
		g_variant_iter_init(&iter, g_variant_builder_end(&locked));
		while (g_variant_iter_next(&iter, "&o", &path))
			g_variant_builder_add(&all, "o", path);
		return g_variant_ref_sink(g_variant_builder_end(&all));
	}

	if (strcmp(propertyName, "Locked") == 0)
		return g_variant_new_boolean(!fKeyStore.IsKeyringUnlocked(
			keyring.String()));

	if (strcmp(propertyName, "Label") == 0)
		return g_variant_new_string(keyring.String());

	if (strcmp(propertyName, "Created") == 0
		|| strcmp(propertyName, "Modified") == 0)
		return g_variant_new_uint64(0);

	return NULL;
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


bool
SecretServiceBridge::Impl::_LoadCollections(
	BObjectList<Collection>& collections)
{
	uint32 cookie = 0;
	BString keyring;
	while (true) {
		status_t result = fKeyStore.GetNextKeyring(cookie, keyring);
		if (result == B_ENTRY_NOT_FOUND)
			break;
		if (result != B_OK)
			return false;

		char path[SS_MAX_NAME * 2 + 32];
		if (ss_CollectionPath(keyring.String(), path, sizeof(path)) == 0) {
			Collection* collection = new Collection;
			if (collection == NULL)
				return false;
			collection->keyring = keyring;
			collection->path = path;
			collections.AddItem(collection);
		}
	}

	return collections.CountItems() > 0;
}


bool
SecretServiceBridge::Impl::_KeyringForPath(const char* path, BString& keyring,
	bool* _isCollection)
{
	if (_isCollection != NULL)
		*_isCollection = false;
	if (path == NULL
		|| strncmp(path, kCollectionPrefix, strlen(kCollectionPrefix)) != 0)
		return false;

	const char* rest = path + strlen(kCollectionPrefix);
	const char* slash = strchr(rest, '/');
	BString collectionName;
	if (slash != NULL)
		collectionName.SetTo(rest, (size_t)(slash - rest));
	else
		collectionName = rest;

	BObjectList<Collection> collections;
	if (!_LoadCollections(collections))
		return false;

	for (int32 i = 0; i < collections.CountItems(); i++) {
		Collection* collection = collections.ItemAt(i);
		char sanitized[SS_MAX_NAME];
		if (ss_SanitizeName(collection->keyring.String(), sanitized,
				sizeof(sanitized)) == 0
			&& collectionName == sanitized) {
			keyring = collection->keyring;
			if (_isCollection != NULL)
				*_isCollection = slash == NULL;
			return true;
		}
	}
	return false;
}


bool
SecretServiceBridge::Impl::_UnlockKeyring(const BString& keyring)
{
	if (fKeyStore.IsKeyringUnlocked(keyring.String()))
		return true;

	// Same path as any BKeyStore client: the server shows the access
	// request and unlock windows for this team.
	uint32 cookie = 0;
	BPasswordKey key;
	fKeyStore.GetNextKey(keyring.String(), B_KEY_TYPE_ANY, B_KEY_PURPOSE_ANY,
		cookie, key);

	return fKeyStore.IsKeyringUnlocked(keyring.String());
}


void
SecretServiceBridge::Impl::_SearchKeyring(const BString& keyring,
	const SsAttributeSet& query, GVariantBuilder* unlocked,
	GVariantBuilder* locked)
{
	if (!fKeyStore.IsKeyringUnlocked(keyring.String()))
		return;

	uint32 cookie = 0;
	BPasswordKey password;
	while (true) {
		status_t result = fKeyStore.GetNextKey(keyring.String(),
			B_KEY_TYPE_ANY, B_KEY_PURPOSE_ANY, cookie, password);
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

		char path[SS_MAX_NAME * 3 + 64];
		if (ss_ItemPath(keyring.String(), password.Identifier(),
				password.SecondaryIdentifier(), path, sizeof(path)) == 0)
			g_variant_builder_add(unlocked, "o", path);
	}
}


void
SecretServiceBridge::Impl::_ReplySearchItems(
	GDBusMethodInvocation* invocation, GVariant* parameters,
	const BString& keyring)
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

	if (keyring.Length() > 0) {
		_SearchKeyring(keyring, query, &unlocked, &locked);
	} else {
		BObjectList<Collection> collections;
		if (_LoadCollections(collections)) {
			for (int32 i = 0; i < collections.CountItems(); i++) {
				Collection* collection = collections.ItemAt(i);
				if (!fKeyStore.IsKeyringUnlocked(
						collection->keyring.String())) {
					char path[SS_MAX_NAME * 2 + 32];
					if (ss_CollectionPath(collection->keyring.String(), path,
							sizeof(path)) == 0)
						g_variant_builder_add(&locked, "o", path);
					continue;
				}
				_SearchKeyring(collection->keyring, query, &unlocked,
					&locked);
			}
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
		bool isCollection = false;
		if (_KeyringForPath(itemPath, keyring, &isCollection)
			&& !isCollection) {
			uint32 cookie = 0;
			BPasswordKey key;
			while (true) {
				status_t result = fKeyStore.GetNextKey(keyring.String(),
					B_KEY_TYPE_PASSWORD, B_KEY_PURPOSE_ANY, cookie, key);
				if (result != B_OK)
					break;

				char path[SS_MAX_NAME * 3 + 64];
				if (ss_ItemPath(keyring.String(), key.Identifier(),
						key.SecondaryIdentifier(), path, sizeof(path)) == 0
					&& strcmp(path, itemPath) == 0 && key.DataLength() > 0) {
					GVariant* value = g_variant_new_fixed_array(
						G_VARIANT_TYPE_BYTE, key.Data(), key.DataLength(),
						1);
					GVariant* params = g_variant_new_array(
						G_VARIANT_TYPE_BYTE, NULL, 0);
					GVariant* secret = g_variant_new("(o@ay@ays)",
						kSecretServicePath, params, value, "text/plain");
					g_variant_builder_add(&secrets, "{o@(oayays)}",
						itemPath, secret);
					break;
				}
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
		bool isCollection = false;
		if (_KeyringForPath(path, keyring, &isCollection)
			&& _UnlockKeyring(keyring))
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
	if (name == NULL || strcmp(name, kDefaultAlias) != 0) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(o)", "/"));
		return;
	}

	BString keyring = kDefaultAlias;
	BObjectList<Collection> collections;
	if (_LoadCollections(collections)) {
		bool found = false;
		for (int32 i = 0; i < collections.CountItems(); i++) {
			if (collections.ItemAt(i)->keyring == kDefaultAlias) {
				found = true;
				break;
			}
		}
		if (!found)
			keyring = kMasterKeyringName;
	} else
		keyring = kMasterKeyringName;

	char path[SS_MAX_NAME * 2 + 32];
	if (ss_CollectionPath(keyring.String(), path, sizeof(path)) != 0) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(o)", "/"));
		return;
	}

	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(o)", path));
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
	BObjectList<Collection> collections;
	if (_LoadCollections(collections)) {
		for (int32 i = 0; i < collections.CountItems(); i++)
			g_variant_builder_add(&builder, "o",
				collections.ItemAt(i)->path.String());
	}
	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(v)", g_variant_builder_end(&builder)));
}


void
SecretServiceBridge::Impl::_ReplyCollectionItems(
	GDBusMethodInvocation* invocation, const BString& keyring)
{
	SsAttributeSet query;
	memset(&query, 0, sizeof(query));
	ss_AttributeSetAdd(&query, "any", "item");

	GVariantBuilder unlocked;
	GVariantBuilder locked;
	g_variant_builder_init(&unlocked, G_VARIANT_TYPE("ao"));
	g_variant_builder_init(&locked, G_VARIANT_TYPE("ao"));
	_SearchKeyring(keyring, query, &unlocked, &locked);

	GVariantBuilder all;
	g_variant_builder_init(&all, G_VARIANT_TYPE("ao"));
	GVariant* unlockedEnd = g_variant_builder_end(&unlocked);
	GVariant* lockedEnd = g_variant_builder_end(&locked);
	GVariantIter iter;
	g_variant_iter_init(&iter, unlockedEnd);
	const gchar* path = NULL;
	while (g_variant_iter_next(&iter, "&o", &path))
		g_variant_builder_add(&all, "o", path);
	g_variant_iter_init(&iter, lockedEnd);
	while (g_variant_iter_next(&iter, "&o", &path))
		g_variant_builder_add(&all, "o", path);

	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(v)", g_variant_builder_end(&all)));
}


void
SecretServiceBridge::Impl::_ReplyCollectionLocked(
	GDBusMethodInvocation* invocation, const BString& keyring)
{
	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(v)", g_variant_new_boolean(
			!fKeyStore.IsKeyringUnlocked(keyring.String()))));
}


void
SecretServiceBridge::Impl::_ReplyCollectionLabel(
	GDBusMethodInvocation* invocation, const BString& keyring)
{
	g_dbus_method_invocation_return_value(invocation,
		g_variant_new("(v)", g_variant_new_string(keyring.String())));
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
		_on_method_call, _on_get_property, NULL, { 0 }
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

	BObjectList<Collection> collections;
	if (_LoadCollections(collections)) {
		for (int32 i = 0; i < collections.CountItems(); i++) {
			Collection* collection = collections.ItemAt(i);
			guint id = g_dbus_connection_register_object(fConnection,
				collection->path.String(), nodeInfo->interfaces[1], &kVTable,
				this, NULL, &error);
			if (id != 0)
				fRegistrations.AddItem(collection);
			else {
				if (error != NULL) {
					g_error_free(error);
					error = NULL;
				}
				delete collection;
			}
		}
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

	for (int32 i = 0; i < fRegistrations.CountItems(); i++)
		delete fRegistrations.ItemAt(i);
	fRegistrations.MakeEmpty();

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
