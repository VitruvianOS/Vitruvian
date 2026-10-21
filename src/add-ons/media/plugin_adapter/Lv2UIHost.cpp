/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "Lv2UIHost.h"

#include <new>
#include <stdio.h>

#include <Message.h>
#include <Messenger.h>

#include "MediaPluginAdapter.h"

#if defined(VOS_HAS_LILV) && defined(VOS_HAS_SUIL)

#include <suil/suil.h>

#include "Lv2PluginState.h"


namespace {


const char* kLv2ExtensionData
	= "http://lv2plug.in/ns/lv2core#extensionData";
const char* kUiShowInterface
	= "http://lv2plug.in/ns/extensions/ui#showInterface";
const char* kUiX11Ui
	= "http://lv2plug.in/ns/extensions/ui#X11UI";


} // anonymous namespace


const LilvUI*
Lv2SelectUi(void* worldPointer, const void* pluginPointer, bool* outExternal,
	const char** outTypeUri)
{
	if (outExternal != NULL)
		*outExternal = false;
	if (outTypeUri != NULL)
		*outTypeUri = NULL;

	LilvWorld* world = (LilvWorld*)worldPointer;
	const LilvPlugin* plugin = (const LilvPlugin*)pluginPointer;
	if (world == NULL || plugin == NULL)
		return NULL;

	const LilvUIs* uis = lilv_plugin_get_uis(plugin);
	if (uis == NULL)
		return NULL;

	LilvNode* extensionData = lilv_new_uri(world, kLv2ExtensionData);
	LilvNode* showInterface = lilv_new_uri(world, kUiShowInterface);
	LilvNode* x11Ui = lilv_new_uri(world, kUiX11Ui);

	const LilvUI* external = NULL;
	const LilvUI* embeddable = NULL;
	const LilvNode* embeddableType = NULL;

	LILV_FOREACH(uis, u, uis) {
		const LilvUI* ui = lilv_uis_get(uis, u);

		if (external == NULL) {
			// An external UI manages its own window; the host only
			// calls show, hide and idle.
			const LilvNode* uiUri = lilv_ui_get_uri(ui);
			lilv_world_load_resource(world, uiUri);
			bool supported = lilv_world_ask(world, uiUri,
				extensionData, showInterface);
			lilv_world_unload_resource(world, uiUri);
			if (supported)
				external = ui;
		}

		if (embeddable == NULL) {
			const LilvNode* type = NULL;
			if (lilv_ui_is_supported(ui, suil_ui_supported, x11Ui,
					&type) > 0) {
				embeddable = ui;
				embeddableType = type;
			}
		}
	}

	const LilvUI* chosen = NULL;
	if (external != NULL) {
		chosen = external;
		if (outExternal != NULL)
			*outExternal = true;
		if (outTypeUri != NULL)
			*outTypeUri = kUiShowInterface;
	} else if (embeddable != NULL) {
		chosen = embeddable;
		if (outTypeUri != NULL && embeddableType != NULL)
			*outTypeUri = lilv_node_as_string(embeddableType);
	}

	lilv_node_free(extensionData);
	lilv_node_free(showInterface);
	lilv_node_free(x11Ui);
	return chosen;
}


Lv2UIHost::Lv2UIHost(BMediaPluginAdapter* owner)
	:
	BLooper("lv2 ui", B_NORMAL_PRIORITY),
	fOwner(owner),
	fSuilHost(NULL),
	fInstance(NULL),
	fIdleRunner(NULL),
	fParentWindow(0),
	fOpen(false)
{
}


Lv2UIHost::~Lv2UIHost()
{
	// The looper thread is gone by now; whatever is left closes here.
	_HandleClose();
	if (fSuilHost != NULL) {
		suil_host_free(fSuilHost);
		fSuilHost = NULL;
	}
}


void
Lv2UIHost::RunUiThread()
{
	Run();
}


bool
Lv2UIHost::HasSupportedUi() const
{
	if (fOwner == NULL)
		return false;
	Lv2PluginState* state = (Lv2PluginState*)fOwner->Lv2State();
	return state != NULL && state->ui != NULL;
}


status_t
Lv2UIHost::Open(uint32 parentWindow)
{
	if (fOwner == NULL || fOwner->Lv2State() == NULL)
		return B_NO_INIT;

	BMessage message(kMsgUiOpen);
	message.AddInt32("parent", (int32)parentWindow);
	BMessage reply;
	status_t result = BMessenger(this).SendMessage(&message, &reply);
	if (result != B_OK)
		return result;

	int32 status = B_ERROR;
	reply.FindInt32("error", &status);
	return (status_t)status;
}


void
Lv2UIHost::Close()
{
	if (!fOpen.load())
		return;

	BMessage message(kMsgUiClose);
	BMessenger(this).SendMessage(&message);
}


void
Lv2UIHost::PortEvent(uint32 portIndex, float value)
{
	if (!fOpen.load())
		return;

	BMessage message(kMsgUiPortEvent);
	message.AddInt32("port", (int32)portIndex);
	message.AddFloat("value", value);
	BMessenger(this).SendMessage(&message);
}


void
Lv2UIHost::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUiOpen:
			message->SendReply(_HandleOpen(message));
			break;

		case kMsgUiClose:
			_HandleClose();
			break;

		case kMsgUiIdle:
			_HandleIdle();
			break;

		case kMsgUiPortEvent:
			_HandlePortEvent(message);
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


status_t
Lv2UIHost::_HandleOpen(BMessage* message)
{
	if (fInstance != NULL)
		return B_OK;

	Lv2PluginState* state = fOwner != NULL
		? (Lv2PluginState*)fOwner->Lv2State() : NULL;
	if (state == NULL || state->instance == NULL)
		return B_NO_INIT;
	if (state->ui == NULL || state->uiTypeUri == NULL)
		return B_NOT_SUPPORTED;

	fParentWindow = 0;
	int32 parent = 0;
	message->FindInt32("parent", &parent);
	fParentWindow = (uint32)parent;

	char* bundlePath = lilv_file_uri_parse(
		lilv_node_as_uri(lilv_ui_get_bundle_uri(state->ui)), NULL);
	char* binaryPath = lilv_file_uri_parse(
		lilv_node_as_uri(lilv_ui_get_binary_uri(state->ui)), NULL);

	// The parent window rides in as a feature; suil hands it to the UI.
	LV2_Feature parentFeature;
	parentFeature.URI = LV2_UI__parent;
	parentFeature.data = (void*)(uintptr_t)fParentWindow;

	const LV2_Feature* uiFeatures[] = {
		&state->featureInstanceAccess,
		&state->featureDataAccess,
		&state->featureIdle,
		fParentWindow != 0 ? &parentFeature : NULL,
		NULL
	};

	if (fSuilHost == NULL) {
		fSuilHost = suil_host_new(&Lv2UIHost::_WriteThunk,
			&Lv2UIHost::_IndexThunk, NULL, NULL);
	}
	if (fSuilHost == NULL) {
		lilv_free(bundlePath);
		lilv_free(binaryPath);
		return B_NO_MEMORY;
	}

	const char* container = kUiX11Ui;
	fInstance = suil_instance_new(fSuilHost, this, container,
		lilv_node_as_uri(lilv_plugin_get_uri(state->plugin)),
		lilv_node_as_uri(lilv_ui_get_uri(state->ui)),
		state->uiTypeUri, bundlePath, binaryPath, uiFeatures);
	if (fInstance == NULL && state->uiExternal) {
		// No wrapper fits the host toolkit; the external UI is its
		// own window, so let it instantiate unwrapped.
		fInstance = suil_instance_new(fSuilHost, this, state->uiTypeUri,
			lilv_node_as_uri(lilv_plugin_get_uri(state->plugin)),
			lilv_node_as_uri(lilv_ui_get_uri(state->ui)),
			state->uiTypeUri, bundlePath, binaryPath, uiFeatures);
	}

	lilv_free(bundlePath);
	lilv_free(binaryPath);

	if (fInstance == NULL)
		return B_NOT_SUPPORTED;

	fOpen = true;

	const LV2UI_Show_Interface* show
		= (const LV2UI_Show_Interface*)suil_instance_extension_data(
			fInstance, LV2_UI__showInterface);
	if (show != NULL)
		show->show(suil_instance_get_handle(fInstance));

	BMessage idle(kMsgUiIdle);
	fIdleRunner = new(std::nothrow) BMessageRunner(BMessenger(this), idle,
		16 * 1000);
	if (fIdleRunner == NULL) {
		_HandleClose();
		return B_NO_MEMORY;
	}
	return B_OK;
}


void
Lv2UIHost::_HandleClose()
{
	if (fIdleRunner != NULL) {
		delete fIdleRunner;
		fIdleRunner = NULL;
	}
	if (fInstance == NULL) {
		fOpen = false;
		return;
	}

	const LV2UI_Show_Interface* show
		= (const LV2UI_Show_Interface*)suil_instance_extension_data(
			fInstance, LV2_UI__showInterface);
	if (show != NULL)
		show->hide(suil_instance_get_handle(fInstance));
	suil_instance_free(fInstance);
	fInstance = NULL;
	fOpen = false;
}


void
Lv2UIHost::_HandleIdle()
{
	if (fInstance == NULL)
		return;

	const LV2UI_Idle_Interface* idle
		= (const LV2UI_Idle_Interface*)suil_instance_extension_data(
			fInstance, LV2_UI__idleInterface);
	if (idle == NULL)
		return;
	if (idle->idle(suil_instance_get_handle(fInstance)) != 0) {
		// The UI closed itself; stop driving it.
		_HandleClose();
	}
}


void
Lv2UIHost::_HandlePortEvent(BMessage* message)
{
	if (fInstance == NULL)
		return;

	int32 port = -1;
	float value = 0.0f;
	if (message->FindInt32("port", &port) != B_OK || port < 0
			|| message->FindFloat("value", &value) != B_OK) {
		return;
	}
	suil_instance_port_event(fInstance, (uint32)port, sizeof(float), 0,
		&value);
}


void
Lv2UIHost::_WriteThunk(void* controller, uint32 portIndex, uint32 bufferSize,
	uint32 protocol, const void* buffer)
{
	Lv2UIHost* self = (Lv2UIHost*)controller;
	if (self == NULL || buffer == NULL || protocol != 0
			|| bufferSize != sizeof(float)) {
		return;
	}
	self->_UiPortWritten(portIndex, *(const float*)buffer);
}


uint32
Lv2UIHost::_IndexThunk(void* controller, const char* symbol)
{
	Lv2UIHost* self = (Lv2UIHost*)controller;
	if (self == NULL || self->fOwner == NULL || symbol == NULL)
		return (uint32)-1;
	return (uint32)self->fOwner->Lv2PortIndexForSymbol(symbol);
}


void
Lv2UIHost::_UiPortWritten(uint32 portIndex, float value)
{
	if (fOwner != NULL)
		fOwner->UiPortWritten(portIndex, value);
}

#endif	// VOS_HAS_LILV && VOS_HAS_SUIL
