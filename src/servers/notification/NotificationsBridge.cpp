/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


// org.freedesktop.Notifications -> BNotification, bus-activated, idle exit.


#include <Application.h>
#include <Bitmap.h>
#include <Notification.h>
#include <String.h>
#include <TranslationUtils.h>

#include <stdio.h>
#include <string.h>

#include <gio/gio.h>


static const gint64 kIdleTimeout = 60 * G_USEC_PER_SEC;

static const gchar kXml[] =
	"<node><interface name='org.freedesktop.Notifications'>"
	"<method name='GetCapabilities'>"
	"<arg type='as' direction='out'/></method>"
	"<method name='Notify'>"
	"<arg type='s' direction='in'/><arg type='u' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='s' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='as' direction='in'/>"
	"<arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
	"<arg type='u' direction='out'/></method>"
	"<method name='CloseNotification'>"
	"<arg type='u' direction='in'/></method>"
	"<method name='GetServerInformation'>"
	"<arg type='s' direction='out'/><arg type='s' direction='out'/>"
	"<arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
	"<signal name='NotificationClosed'>"
	"<arg type='u'/><arg type='u'/></signal>"
	"</interface></node>";

static gint64 sLastCall;
static guint32 sNextID = 1;
static GMainLoop* sLoop;


static BString
StripMarkup(const char* in)
{
	BString out;
	bool tag = false;
	for (const char* p = in; *p; p++) {
		if (*p == '<') {
			tag = true;
		} else if (*p == '>' && tag) {
			tag = false;
		} else if (!tag) {
			out << *p;
		}
	}
	out.ReplaceAll("&lt;", "<");
	out.ReplaceAll("&gt;", ">");
	out.ReplaceAll("&quot;", "\"");
	out.ReplaceAll("&apos;", "'");
	out.ReplaceAll("&amp;", "&");
	return out;
}


static BBitmap*
IconFor(const char* icon, GVariant* hints)
{
	GVariant* data = g_variant_lookup_value(hints, "image-data",
		G_VARIANT_TYPE("(iiibiiay)"));
	if (data == NULL) {
		data = g_variant_lookup_value(hints, "icon_data",
			G_VARIANT_TYPE("(iiibiiay)"));
	}
	if (data != NULL) {
		gint32 w, h, stride, bits, channels;
		gboolean alpha;
		GVariant* bytes;
		g_variant_get(data, "(iiibii@ay)", &w, &h, &stride, &alpha, &bits,
			&channels, &bytes);
		gsize size = 0;
		const uint8* src = (const uint8*)g_variant_get_fixed_array(bytes,
			&size, 1);
		BBitmap* bitmap = NULL;
		if (bits == 8 && channels >= 3 && w > 0 && h > 0
			&& size >= (gsize)stride * (h - 1) + (gsize)w * channels) {
			bitmap = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGBA32);
			if (bitmap->InitCheck() == B_OK) {
				uint8* dst = (uint8*)bitmap->Bits();
				for (int32 y = 0; y < h; y++) {
					for (int32 x = 0; x < w; x++) {
						const uint8* s = src + y * stride + x * channels;
						uint8* d = dst + y * bitmap->BytesPerRow() + x * 4;
						d[0] = s[2];
						d[1] = s[1];
						d[2] = s[0];
						d[3] = channels == 4 ? s[3] : 255;
					}
				}
			} else {
				delete bitmap;
				bitmap = NULL;
			}
		}
		g_variant_unref(bytes);
		g_variant_unref(data);
		if (bitmap != NULL)
			return bitmap;
	}

	if (icon != NULL && *icon != '\0') {
		const char* path = icon;
		if (strncmp(path, "file://", 7) == 0)
			path += 7;
		if (path[0] == '/')
			return BTranslationUtils::GetBitmapFile(path);
	}
	return NULL;
}


static void
HandleCall(GDBusConnection* connection, const gchar*, const gchar*,
	const gchar*, const gchar* method, GVariant* params,
	GDBusMethodInvocation* invocation, gpointer)
{
	sLastCall = g_get_monotonic_time();

	if (strcmp(method, "GetCapabilities") == 0) {
		const gchar* caps[] = { "body", NULL };
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(^as)", caps));
	} else if (strcmp(method, "GetServerInformation") == 0) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(ssss)", "V\\OS", "V\\OS", "1.0", "1.2"));
	} else if (strcmp(method, "CloseNotification") == 0) {
		guint32 id;
		g_variant_get(params, "(u)", &id);
		g_dbus_connection_emit_signal(connection, NULL,
			"/org/freedesktop/Notifications",
			"org.freedesktop.Notifications", "NotificationClosed",
			g_variant_new("(uu)", id, 3), NULL);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (strcmp(method, "Notify") == 0) {
		const gchar *app, *icon, *summary, *body;
		guint32 replaces;
		gint32 timeout;
		GVariant* hints;
		g_variant_get(params, "(&su&s&s&s^as@a{sv}i)", &app, &replaces,
			&icon, &summary, &body, NULL, &hints, &timeout);

		guint32 id = replaces != 0 ? replaces : sNextID++;

		guchar urgency = 1;
		g_variant_lookup(hints, "urgency", "y", &urgency);
		BNotification notification(urgency >= 2
			? B_IMPORTANT_NOTIFICATION : B_INFORMATION_NOTIFICATION);
		notification.SetGroup(app);
		notification.SetTitle(StripMarkup(summary));
		notification.SetContent(StripMarkup(body));
		BString messageID;
		messageID << "fdo-" << (int32)id;
		notification.SetMessageID(messageID);
		BBitmap* bitmap = IconFor(icon, hints);
		if (bitmap != NULL) {
			notification.SetIcon(bitmap);
			delete bitmap;
		}
		notification.Send();
		g_variant_unref(hints);

		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(u)", id));
	} else {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
			G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
	}
}


static gboolean
IdleCheck(gpointer)
{
	if (g_get_monotonic_time() - sLastCall > kIdleTimeout)
		g_main_loop_quit(sLoop);
	return G_SOURCE_CONTINUE;
}


static void
BusAcquired(GDBusConnection* connection, const gchar*, gpointer)
{
	GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kXml, NULL);
	static const GDBusInterfaceVTable vtable = { HandleCall, NULL, NULL };
	g_dbus_connection_register_object(connection,
		"/org/freedesktop/Notifications", info->interfaces[0], &vtable, NULL,
		NULL, NULL);
}


static void
NameLost(GDBusConnection*, const gchar*, gpointer)
{
	g_main_loop_quit(sLoop);
}


int
main()
{
	BApplication app("application/x-vnd.vos-notifications_bridge");

	sLastCall = g_get_monotonic_time();
	sLoop = g_main_loop_new(NULL, FALSE);
	g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
		G_BUS_NAME_OWNER_FLAGS_NONE, BusAcquired, NULL, NameLost, NULL, NULL);
	g_timeout_add_seconds(5, IdleCheck, NULL);
	g_main_loop_run(sLoop);
	return 0;
}
