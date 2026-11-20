/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "StoreWindow.h"
#include "StoreDefs.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Entry.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Path.h>
#include <Roster.h>
#include <FindDirectory.h>
#include <DataIO.h>
#include <Directory.h>
#include <Url.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <UrlProtocolRoster.h>

#include <package/PackageInfo.h>
#include <package/PackageRoster.h>

#include <web/WebView.h>
#include <web/WebDefs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace BPrivate::Network;

#include "PackageWorker.h"
#include "PackageManagerDefs.h"
#include "ChangeSummaryWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StoreWindow"


static const char* kDataDirEnv = "STORE_DATA_DIR";
static const char* kDefaultDataDir = "/system/data/store";

// The app section's category filter: catalog category, shown label.
// Only the categories the catalog actually uses are listed.
static const struct {
	const char*	category;
	const char*	label;
} kAppCategories[] = {
	{ "Network", "Internet" },
	{ "Graphics", "Graphics" },
	{ "AudioVideo", "Media" },
	{ "Game", "Games" },
	{ "Office", "Office" },
	{ "Development", "Development" },
	{ "Utility", "Utilities" },
	{ "System", "System" },
	{ NULL, NULL }
};


static const char* kCatalogURL = "https://store.v-os.dev/catalog.json";
static const char* kSiteURL = "https://store.v-os.dev";


static const char*
_categoryLabel(const BString& category)
{
	for (int32 i = 0; kAppCategories[i].category != NULL; i++) {
		if (category == kAppCategories[i].category)
			return kAppCategories[i].label;
	}
	return category.String();
}


//	#pragma mark - Local helpers


static BString
_jsonEscape(const BString& input)
{
	BString result;
	const char* p = input.String();
	for (; *p != '\0'; p++) {
		unsigned char c = (unsigned char)*p;
		switch (c) {
			case '"': result << "\\\""; break;
			case '\\': result << "\\\\"; break;
			case '\n': result << "\\n"; break;
			case '\r': result << "\\r"; break;
			case '\t': result << "\\t"; break;
			default:
				if (c < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					result << buffer;
				} else
					result << (char)c;
				break;
		}
	}
	return result;
}


static BString
_dataDir()
{
	const char* env = getenv(kDataDirEnv);
	if (env != NULL && env[0] != '\0')
		return BString(env);
	return BString(kDefaultDataDir);
}


//	#pragma mark - StoreWindow


StoreWindow::StoreWindow(BRect frame, bool dryRun)
	:
	BWindow(frame, B_TRANSLATE("Store"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fWebView(NULL),
	fWorker(NULL),
	fRoster(NULL),
	fCatalog(),
	fSection("app"),
	fSelectedCategory("All"),
	fInstalledPackages(32),
	fPendingStateQueries(0),
	fBusyPercent(-1),
	fDryRun(dryRun)
{
	fRoster = new VPackageRoster();
	fWorker = new PackageWorker(BMessenger(this));
	fWorker->Run();

	fWebView = new BWebView(Bounds(), "store");
	fWebView->SetTarget(BMessenger(this));
	fWebView->AddScriptMessageHandler("store");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fWebView);

	_LoadCatalog();
	_FetchCatalog();

	// Defer the page load until the window (and the web view's
	// bridge) is attached.
	PostMessage(kStoreMsgLoadPage);
}


StoreWindow::~StoreWindow()
{
	if (fWorker != NULL) {
		if (fWorker->Lock())
			fWorker->Quit();
	}
	delete fWorker;
	delete fRoster;
}


bool
StoreWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
StoreWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kStoreMsgLoadPage:
			_LoadPage();
			break;

		case kStoreMsgScriptDone:
			// EvaluateJavaScript reply; the result string is of no
			// interest, every push is one-way.
			break;

		case kStoreMsgCatalogFetched:
		{
			const char* json = NULL;
			if (message->FindString("json", &json) == B_OK
				&& fBusyEntryId.IsEmpty()
				&& fCatalog.LoadFromData(json) == B_OK) {
				_ReloadStates();
			}
			break;
		}

		case B_WEB_PAGE_LOAD_FINISHED:
			_ReloadStates();
			break;

		case B_WEB_SCRIPT_MESSAGE:
		{
			const char* handler = NULL;
			const char* payload = NULL;
			if (message->FindString(B_WEB_HANDLER_FIELD, &handler) != B_OK
				|| handler == NULL || strcmp(handler, "store") != 0) {
				break;
			}
			if (message->FindString(B_WEB_BODY_FIELD, &payload) == B_OK)
				_DispatchPageAction(payload);
			break;
		}

		case kVMsgPackageDetailsReply:
		{
			void* pointer = NULL;
			if (message->FindPointer("package", &pointer) != B_OK
				|| pointer == NULL) {
				break;
			}
			// The reply transfers one reference to us.
			VPackageInfoRef ref((VPackageInfo*)pointer, true);

			if (ref->State() == V_PACKAGE_STATE_INSTALLED
				|| ref->State() == V_PACKAGE_STATE_UPGRADABLE) {
				fInstalledPackages.AddItem(
					new BString(ref->Name().String()));
			}

			if (fPendingStateQueries > 0)
				fPendingStateQueries--;
			if (fPendingStateQueries == 0)
				_PushState();
			break;
		}

		case kVMsgQueryError:
			// Unknown package: simply not installed.
			if (fPendingStateQueries > 0)
				fPendingStateQueries--;
			if (fPendingStateQueries == 0)
				_PushState();
			break;

		case kMsgSimulateReady:
		{
			if (fDryRun) {
				const char* summary = "";
				message->FindString("summary", &summary);
				BString status(B_TRANSLATE("Dry run: "));
				status << summary;
				_SetStatus(status);
				_FinishBusy();
				break;
			}

			// Same review dialog and apply request as PackageManager.
			fPendingApply = BMessage(kMsgApplyChanges);
			const char* name = NULL;
			for (int32 i = 0; message->FindString("install", i, &name) == B_OK;
					i++)
				fPendingApply.AddString("install", name);
			for (int32 i = 0; message->FindString("remove", i, &name) == B_OK;
					i++)
				fPendingApply.AddString("remove", name);
			for (int32 i = 0; message->FindString("purge", i, &name) == B_OK;
					i++)
				fPendingApply.AddString("purge", name);

			if (fPendingApply.FindString("install", 0, &name) != B_OK
				&& fPendingApply.FindString("remove", 0, &name) != B_OK
				&& fPendingApply.FindString("purge", 0, &name) != B_OK) {
				// Nothing to do (already installed/absent).
				_SetStatus(B_TRANSLATE("No changes needed"));
				_FinishBusy();
				break;
			}

			const char* summary = "";
			message->FindString("summary", &summary);
			BMessage details;
			message->FindMessage("details", &details);
			ChangeSummaryWindow* window = new ChangeSummaryWindow(this,
				BMessenger(this), &details, summary);
			window->Show();
			break;
		}

		case kMsgSummaryApply:
			fWorker->PostMessage(&fPendingApply);
			break;

		case kMsgSummaryCancel:
			fPendingApply.MakeEmpty();
			_SetStatus(B_TRANSLATE("Cancelled"));
			_FinishBusy();
			break;

		case kMsgProgress:
		{
			int32 percent = -1;
			const char* status = "";
			message->FindInt32("percent", &percent);
			message->FindString("status", &status);
			const char* package = "";
			message->FindString("package", &package);
			fBusyPercent = percent;
			fBusyStatus = status;
			if (package[0] != '\0')
				fBusyStatus << " " << package;

			BString script("Store.onProgress(");
			script << percent << ", \"" << _jsonEscape(fBusyStatus) << "\")";
			_RunScript(script.String());
			break;
		}

		case kMsgTransactionDone:
			_SetStatus(B_TRANSLATE("Transaction finished"));
			_FinishBusy();
			break;

		case kMsgError:
		{
			int32 error = kAptErrorUnknown;
			const char* detail = "";
			message->FindInt32("error", &error);
			message->FindString("detail", &detail);

			BString text;
			switch (error) {
				case kAptErrorLockHeld:
					text = B_TRANSLATE("Another program is using the package "
						"system. Close it and try again.");
					break;
				case kAptErrorNoPrivilege:
					text = B_TRANSLATE("Authentication failed or was "
						"cancelled. Nothing was changed.");
					break;
				case kAptErrorBrokenDeps:
					text = B_TRANSLATE("These changes cannot be applied "
						"without breaking other packages.");
					break;
				case kAptErrorNetwork:
					text = B_TRANSLATE("Could not reach the software "
						"repositories.");
					break;
				case kAptErrorSignature:
					text = B_TRANSLATE("A repository failed its signature "
						"check. Nothing was installed.");
					break;
				case kAptErrorDiskFull:
					text = B_TRANSLATE("There is not enough free disk space.");
					break;
				case kAptErrorInterrupted:
					text = B_TRANSLATE("The operation was interrupted.");
					break;
				case kAptErrorBatchTooLarge:
					text = B_TRANSLATE("Too many packages for a single "
						"action.");
					break;
				default:
					text = B_TRANSLATE("The operation failed.");
					break;
			}
			if (detail[0] != '\0')
				text << "\n\n" << detail;

			_SetStatus(text);
			_FinishBusy();
			BAlert* alert = new BAlert(B_TRANSLATE("Package manager"),
				text.String(), B_TRANSLATE("OK"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go(NULL);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


//	#pragma mark - Catalog


static status_t
_cachePath(BPath& path)
{
	status_t status = find_directory(B_USER_CACHE_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	return path.Append("store/catalog.json");
}


// The cached copy of the site catalog if it parses, else the bundled one.
void
StoreWindow::_LoadCatalog()
{
	BPath cache;
	if (_cachePath(cache) == B_OK
		&& fCatalog.LoadFrom(cache.Path()) == B_OK
		&& fCatalog.CountEntries() > 0) {
		return;
	}

	BString bundled = _dataDir();
	bundled << "/catalog.json";
	status_t status = fCatalog.LoadFrom(bundled.String());
	if (status != B_OK) {
		fprintf(stderr, "Store: cannot load catalog from %s: %s\n",
			bundled.String(), strerror(status));
	}
}


void
StoreWindow::_FetchCatalog()
{
	thread_id thread = spawn_thread(&_FetchThread, "store catalog",
		B_LOW_PRIORITY, new BMessenger(this));
	if (thread >= 0)
		resume_thread(thread);
}


// Downloads catalog.json; a valid copy is cached and handed to the
// window. Failures are silent: the cached or bundled catalog stays.
status_t
StoreWindow::_FetchThread(void* data)
{
	BMessenger* target = (BMessenger*)data;

	BMallocIO* output = new BMallocIO();
	BUrl url(kCatalogURL, true);
	BHttpRequest* request = dynamic_cast<BHttpRequest*>(
		BUrlProtocolRoster::MakeRequest(url, output));
	if (request == NULL) {
		delete output;
		delete target;
		return B_ERROR;
	}

	// Run() returns the request thread id.
	thread_id requestThread = request->Run();
	if (requestThread >= 0) {
		status_t exitValue;
		wait_for_thread(requestThread, &exitValue);

		const BHttpResult& result
			= static_cast<const BHttpResult&>(request->Result());
		if (result.StatusCode() == 200 && output->BufferLength() > 0) {
			BString json((const char*)output->Buffer(),
				output->BufferLength());
			StoreCatalog check;
			if (check.LoadFromData(json.String()) == B_OK
				&& check.CountEntries() > 0) {
				BPath cache;
				if (_cachePath(cache) == B_OK) {
					BPath parent;
					cache.GetParent(&parent);
					create_directory(parent.Path(), 0755);
					BFile file(cache.Path(),
						B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
					if (file.InitCheck() == B_OK)
						file.Write(json.String(), json.Length());
				}
				BMessage reply(kStoreMsgCatalogFetched);
				reply.AddString("json", json.String());
				target->SendMessage(&reply);
			}
		}
	}

	delete request;
	delete output;
	delete target;
	return B_OK;
}


//	#pragma mark - Page load and actions


void
StoreWindow::_LoadPage()
{
	BString dataPath = _dataDir();

	BString html;
	BPath cssPath(dataPath.String(), "store.css", NULL);
	BPath jsPath(dataPath.String(), "store.js", NULL);
	BPath htmlPath(dataPath.String(), "index.html", NULL);

	BFile cssFile(cssPath.Path(), B_READ_ONLY);
	BFile jsFile(jsPath.Path(), B_READ_ONLY);
	BFile htmlFile(htmlPath.Path(), B_READ_ONLY);

	BString css;
	BString js;
	char buffer[4096];
	ssize_t bytes;
	while ((bytes = cssFile.Read(buffer, sizeof(buffer))) > 0)
		css.Append(buffer, bytes);
	while ((bytes = jsFile.Read(buffer, sizeof(buffer))) > 0)
		js.Append(buffer, bytes);
	while ((bytes = htmlFile.Read(buffer, sizeof(buffer))) > 0)
		html.Append(buffer, bytes);

	if (html.IsEmpty()) {
		fprintf(stderr, "Store: cannot read %s\n", htmlPath.Path());
		html = "<html><body><p>Store template missing.</p></body></html>";
	}

	// Inline the local CSS/JS so the page needs no network and no
	// file:// subresource loads.
	int32 cssAt = html.FindFirst("<style>/*store-css*/</style>");
	if (cssAt >= 0)
		html.ReplaceFirst("<style>/*store-css*/</style>",
			BString("<style>") << css << "</style>");

	int32 jsAt = html.FindFirst("<script>/*store-js*/</script>");
	if (jsAt >= 0)
		html.ReplaceFirst("<script>/*store-js*/</script>",
			BString("<script>") << js << "</script>");

	fWebView->LoadHTML(html.String(), "file:///");
}


void
StoreWindow::_DispatchPageAction(const char* payload)
{
	if (payload == NULL)
		return;

	BString action(payload);
	int32 bar = action.FindFirst('|');
	BString argument;
	if (bar >= 0) {
		action.CopyInto(argument, bar + 1, action.Length());
		action.Truncate(bar);
	}

	if (action == "ready") {
		_PushState();
	} else if (action == "section") {
		if (argument == "app" || argument == "component"
			|| argument == "data") {
			fSection = argument;
			fSelectedCategory = "All";
			fInfoEntryId = "";
			_PushState();
		}
	} else if (action == "info") {
		fInfoEntryId = argument;
		_PushState();
	} else if (action == "select") {
		fSelectedCategory = argument;
		_PushState();
	} else if (action == "install" || action == "remove") {
		StoreEntry* entry = fCatalog.FindById(argument.String());
		if (entry == NULL)
			return;
		_StartTransaction(action == "install", entry);
	}
}


void
StoreWindow::_ReloadStates()
{
	fInstalledPackages.MakeEmpty();
	fPendingStateQueries = 0;

	// One roster query per unique catalog package; replies rebuild
	// the installed set and then push fresh state to the page.
	for (int32 i = 0; i < fCatalog.CountEntries(); i++) {
		StoreEntry* entry = fCatalog.EntryAt(i);
		for (int32 p = 0; p < entry->fPackages.CountItems(); p++) {
			BString* packageName = entry->fPackages.ItemAt(p);
			if (fRoster->GetPackageInfo(packageName->String(),
					BMessenger(this)) == B_OK) {
				fPendingStateQueries++;
			}
		}
	}

	if (fPendingStateQueries == 0)
		_PushState();
}


void
StoreWindow::_StartTransaction(bool install, StoreEntry* entry)
{
	if (!fBusyEntryId.IsEmpty())
		return;

	// Required components come first, then the entry's own packages.
	BObjectList<BString, true> packages;
	if (install) {
		for (int32 i = 0; i < entry->fRequires.CountItems(); i++) {
			StoreEntry* component
				= fCatalog.FindById(entry->fRequires.ItemAt(i)->String());
			if (component == NULL)
				continue;
			for (int32 p = 0; p < component->fPackages.CountItems(); p++) {
				packages.AddItem(
					new BString(*component->fPackages.ItemAt(p)));
			}
		}
	}
	for (int32 i = 0; i < entry->fPackages.CountItems(); i++)
		packages.AddItem(new BString(*entry->fPackages.ItemAt(i)));

	BMessage simulate(kMsgSimulate);
	const char* field = install ? "install" : "remove";
	for (int32 i = 0; i < packages.CountItems(); i++)
		simulate.AddString(field, packages.ItemAt(i)->String());

	fBusyEntryId = entry->fId;
	fBusyPercent = -1;
	fBusyStatus = install
		? B_TRANSLATE("Simulating installation")
		: B_TRANSLATE("Simulating removal");
	_PushState();

	fWorker->PostMessage(&simulate);
}


void
StoreWindow::_SetStatus(const BString& status)
{
	BString script("Store.setStatus(\"");
	script << _jsonEscape(status) << "\")";
	_RunScript(script.String());
}


void
StoreWindow::_FinishBusy()
{
	fBusyEntryId = "";
	fBusyPercent = -1;
	fBusyStatus = "";
	_ReloadStates();
}


void
StoreWindow::_RunScript(const char* script)
{
	if (fWebView == NULL)
		return;

	fWebView->EvaluateJavaScript(script, BMessenger(this),
		kStoreMsgScriptDone);
}


//	#pragma mark - State pushed to the page


bool
StoreWindow::_IsInstalled(StoreEntry* entry) const
{
	if (entry->fPackages.CountItems() == 0)
		return false;

	for (int32 i = 0; i < entry->fPackages.CountItems(); i++) {
		BString* packageName = entry->fPackages.ItemAt(i);
		bool found = false;
		for (int32 j = 0; j < fInstalledPackages.CountItems(); j++) {
			if (*fInstalledPackages.ItemAt(j) == *packageName) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}


BString
StoreWindow::_EntryIconDataURI(StoreEntry* entry)
{
	// An icon from the site, by path or absolute URL.
	if (!entry->fIcon.IsEmpty()) {
		if (entry->fIcon[0] == '/')
			return BString(kSiteURL) << entry->fIcon;
		return entry->fIcon;
	}

	return BString();
}


BString
StoreWindow::_BuildCategoriesJSON()
{
	// Components and data are shown as flat lists; only the apps
	// section has a category filter.
	BString json("[\"All\"");
	if (fSection == "app") {
		for (int32 i = 0; kAppCategories[i].category != NULL; i++) {
			bool used = false;
			for (int32 j = 0; j < fCatalog.CountEntries(); j++) {
				StoreEntry* entry = fCatalog.EntryAt(j);
				if (entry->fKind == "app"
					&& entry->fCategory == kAppCategories[i].category) {
					used = true;
					break;
				}
			}
			if (used)
				json << ",\"" << kAppCategories[i].label << "\"";
		}
	}
	json << "]";
	return json;
}


BString
StoreWindow::_BuildStateJSON()
{
	BString json("{");
	json << "\"sections\":[\"app\",\"component\",\"data\"]";
	json << ",\"section\":\"" << _jsonEscape(fSection) << "\"";
	json << ",\"categories\":" << _BuildCategoriesJSON();
	json << ",\"selected\":\"" << _jsonEscape(fSelectedCategory) << "\"";
	json << ",\"info\":\"" << _jsonEscape(fInfoEntryId) << "\",";

	if (!fBusyEntryId.IsEmpty()) {
		json << "\"busy\":{\"id\":\"" << _jsonEscape(fBusyEntryId)
			<< "\",\"percent\":" << fBusyPercent
			<< ",\"status\":\"" << _jsonEscape(fBusyStatus) << "\"},";
	} else
		json << "\"busy\":null,";

	json << "\"entries\":[";
	bool first = true;
	for (int32 i = 0; i < fCatalog.CountEntries(); i++) {
		StoreEntry* entry = fCatalog.EntryAt(i);

		// One section per kind; within the apps section the category
		// filter applies.
		if (entry->fKind != fSection)
			continue;
		if (fSection == "app" && fSelectedCategory != "All"
			&& fSelectedCategory != _categoryLabel(entry->fCategory)) {
			continue;
		}

		if (!first)
			json << ",";
		first = false;

		bool installed = _IsInstalled(entry);
		BString icon = _EntryIconDataURI(entry);

		json << "{\"id\":\"" << _jsonEscape(entry->fId) << "\"";
		json << ",\"kind\":\"" << _jsonEscape(entry->fKind) << "\"";
		json << ",\"name\":\"" << _jsonEscape(entry->fName) << "\"";
		json << ",\"summary\":\"" << _jsonEscape(entry->fSummary) << "\"";
		json << ",\"category\":\""
			<< _jsonEscape(_categoryLabel(entry->fCategory)) << "\"";
		json << ",\"installed\":" << (installed ? "true" : "false");
		BString needs;
		for (int32 r = 0; r < entry->fRequires.CountItems(); r++) {
			StoreEntry* component
				= fCatalog.FindById(entry->fRequires.ItemAt(r)->String());
			if (r > 0)
				needs << ", ";
			needs << (component != NULL
				? component->fName : *entry->fRequires.ItemAt(r));
		}
		json << ",\"requires\":\"" << _jsonEscape(needs) << "\"";
		json << ",\"packages\":[";
		for (int32 k = 0; k < entry->fPackages.CountItems(); k++) {
			if (k > 0)
				json << ",";
			json << "\"" << _jsonEscape(*entry->fPackages.ItemAt(k)) << "\"";
		}
		json << "]";
		if (!icon.IsEmpty())
			json << ",\"icon\":\"" << icon << "\"";
		json << "}";
	}
	json << "]}";
	return json;
}


void
StoreWindow::_PushState()
{
	BString script("Store.updateState(");
	script << _BuildStateJSON() << ")";
	_RunScript(script.String());
}
