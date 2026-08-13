#include "ConnectivityServer.h"
#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Looper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>


class ConnectivityServer : public BApplication {
public:
	ConnectivityServer();
	virtual ~ConnectivityServer();
	
	virtual void ReadyToRun();
	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();
	
private:
	void _InitNetworkManager();
	void _InitBlueZ();
	void _HandleNMMessage(sd_bus_message* msg);
	void _HandleBlueZMessage(sd_bus_message* msg);
	
	sd_bus* fSystemBus;
	sd_bus_slot* fNMSubscription;
	sd_bus_slot* fBlueZSubscription;
	
	static const char* kSignature = "application/x-vnd.Vitruvian-ConnectivityServer";
};


static ConnectivityServer* sInstance = NULL;


ConnectivityServer::ConnectivityServer()
	: BApplication(kSignature),
	fSystemBus(NULL),
	fNMSubscription(NULL),
	fBlueZSubscription(NULL)
{
	sInstance = this;
}


ConnectivityServer::~ConnectivityServer()
{
	if (fNMSubscription) {
		sd_bus_slot_unref(fNMSubscription);
	}
	if (fBlueZSubscription) {
		sd_bus_slot_unref(fBlueZSubscription);
	}
	if (fSystemBus) {
		sd_bus_flush_close_unref(fSystemBus);
	}
}


void
ConnectivityServer::ReadyToRun()
{
	// Connect to system D-Bus
	int result = sd_bus_open_system(&fSystemBus);
	if (result < 0) {
		fprintf(stderr, "Failed to connect to system bus: %s\n",
			strerror(-result));
		return;
	}
	
	// Initialize NetworkManager bridge
	_InitNetworkManager();
	
	// Initialize BlueZ bridge
	_InitBlueZ();
	
	printf("ConnectivityServer ready - NetworkManager and BlueZ bridges active\n");
}


void
ConnectivityServer::_InitNetworkManager()
{
	// Subscribe to NetworkManager state changes
	int result = sd_bus_add_match(fSystemBus, &fNMSubscription,
		"type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.DBus.Properties'",
		NULL, NULL);
	if (result < 0) {
		fprintf(stderr, "Failed to subscribe to NetworkManager signals: %s\n",
			strerror(-result));
	}
	
	printf("NetworkManager bridge initialized\n");
}


void
ConnectivityServer::_InitBlueZ()
{
	// Subscribe to BlueZ state changes
	int result = sd_bus_add_match(fSystemBus, &fBlueZSubscription,
		"type='signal',sender='org.bluez'",
		NULL, NULL);
	if (result < 0) {
		fprintf(stderr, "Failed to subscribe to BlueZ signals: %s\n",
			strerror(-result));
	}
	
	printf("BlueZ bridge initialized\n");
}


void
ConnectivityServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'GETD': // Get devices
			{
				BMessage reply('DEV!');
				// TODO: Query NetworkManager for devices and populate reply
				SendReply(&reply);
			}
			break;
			
		case 'CONN': // Connect device
			{
				const char* deviceId;
				if (message->FindString("device_id", &deviceId) == B_OK) {
					// TODO: Call NetworkManager to connect device
					BMessage reply('CONN');
					SendReply(&reply);
				}
			}
			break;
			
		case 'DISC': // Disconnect device
			{
				const char* deviceId;
				if (message->FindString("device_id", &deviceId) == B_OK) {
					// TODO: Call NetworkManager to disconnect device
					BMessage reply('DISC');
					SendReply(&reply);
				}
			}
			break;
			
		case 'SCAN': // Scan WiFi networks
			{
				const char* deviceId;
				if (message->FindString("device_id", &deviceId) == B_OK) {
					// TODO: Call NetworkManager to scan for wireless networks
					BMessage reply('SCAN');
					SendReply(&reply);
				}
			}
			break;
			
		case 'JOIN': // Join WiFi network
			{
				const char* deviceId;
				const char* ssid;
				const char* password;
				if (message->FindString("device_id", &deviceId) == B_OK &&
					message->FindString("ssid", &ssid) == B_OK) {
					message->FindString("password", &password);
					// TODO: Call NetworkManager to join WiFi network
					BMessage reply('JOIN');
					SendReply(&reply);
				}
			}
			break;
			
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool
ConnectivityServer::QuitRequested()
{
	// Don't allow quit while running as a server
	return false;
}


int
main()
{
	ConnectivityServer server;
	server.Run();
	return 0;
}