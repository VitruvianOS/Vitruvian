/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "StoreWindow.h"
#include "StoreDefs.h"

#include <Alert.h>
#include <Application.h>
#include <Looper.h>
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

#include <web/WebView.h>
#include <web/WebDefs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace BPrivate::Network;

#include "PackageErrors.h"
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


static const int32 kPageSize = 60;
static const int32 kIconsNotifyEvery = 8;

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


enum {
	kCancelNone			= 0,
	kCancelAvailable	= 1,
	kCancelRequested	= 2
};


//	#pragma mark - Icons and installed state


static status_t
_iconCacheDir(BPath& path)
{
	status_t status = find_directory(B_USER_CACHE_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	return path.Append("store/icons");
}


static BString
_iconURL(const BString& icon)
{
	if (!icon.IsEmpty() && icon[0] == '/')
		return BString(kSiteURL) << icon;
	return icon;
}


// Cache file name for an icon URL: every non-alphanumeric byte (but the
// extension dot) becomes an underscore.
static BString
_iconFileName(const BString& url)
{
	BString name;
	for (int32 i = 0; i < url.Length(); i++) {
		char c = url[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '.';
		name << (ok ? c : '_');
	}
	return name;
}


// The single icon download worker: requests are served in order, one
// download at a time, at low priority. A newer request supersedes the
// one in flight (the page moved on); failed URLs are not retried.
class IconFetcher : public BLooper {
public:
	IconFetcher(const BMessenger& target)
		:
		BLooper("store icons", B_LOW_PRIORITY),
		fTarget(target),
		fGeneration(0)
	{
	}

	void Fetch(BMessage* urls)
	{
		urls->what = kStoreMsgFetchIcons;
		urls->AddInt32("gen", atomic_add(&fGeneration, 1) + 1);
		PostMessage(urls);
	}

	void Cancel()
	{
		atomic_add(&fGeneration, 1);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kStoreMsgFetchIcons) {
			BLooper::MessageReceived(message);
			return;
		}

		int32 generation = 0;
		message->FindInt32("gen", &generation);

		BPath dir;
		if (_iconCacheDir(dir) != B_OK)
			return;
		create_directory(dir.Path(), 0755);

		int32 pending = 0;
		const char* urlString;
		for (int32 i = 0; message->FindString("url", i, &urlString) == B_OK;
				i++) {
			if (atomic_get(&fGeneration) != generation)
				return;

			BString url(urlString);
			if (fFailed.count(url.String()) > 0)
				continue;
			BPath file(dir.Path(), _iconFileName(url).String());
			BEntry existing(file.Path());
			if (existing.Exists())
				continue;

			if (_Download(url, file))
				pending++;
			else
				fFailed.insert(url.String());

			if (pending >= kIconsNotifyEvery) {
				fTarget.SendMessage(kStoreMsgIconsFetched);
				pending = 0;
			}
		}
		if (pending > 0)
			fTarget.SendMessage(kStoreMsgIconsFetched);
	}

private:
	bool _Download(const BString& url, const BPath& file)
	{
		BMallocIO output;
		BUrl remote(url.String(), true);
		BHttpRequest* request = dynamic_cast<BHttpRequest*>(
			BUrlProtocolRoster::MakeRequest(remote, &output));
		if (request == NULL)
			return false;

		bool done = false;
		thread_id requestThread = request->Run();
		if (requestThread >= 0) {
			status_t exitValue;
			wait_for_thread(requestThread, &exitValue);
			const BHttpResult& result
				= static_cast<const BHttpResult&>(request->Result());
			if (result.StatusCode() == 200 && output.BufferLength() > 0) {
				BString temp(file.Path());
				temp << ".part";
				BFile out(temp.String(),
					B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
				if (out.InitCheck() == B_OK
					&& out.Write(output.Buffer(), output.BufferLength())
						== (ssize_t)output.BufferLength()) {
					BEntry part(temp.String());
					done = part.Rename(file.Path(), true) == B_OK;
				}
			}
		}
		delete request;
		return done;
	}

	BMessenger			fTarget;
	int32				fGeneration;
	std::set<std::string> fFailed;
};


// Reads the installed package names from the dpkg status file in one
// pass and hands them to the window.
static status_t
_InstalledThread(void* data)
{
	BMessage* request = (BMessage*)data;
	BMessenger target;
	request->FindMessenger("target", &target);
	int32 seq = 0;
	request->FindInt32("seq", &seq);
	delete request;

	std::set<std::string>* installed = new std::set<std::string>();
	FILE* file = fopen("/var/lib/dpkg/status", "r");
	if (file != NULL) {
		char* line = NULL;
		size_t capacity = 0;
		std::string name;
		ssize_t length;
		while ((length = getline(&line, &capacity, file)) >= 0) {
			while (length > 0 && (line[length - 1] == '\n'
					|| line[length - 1] == '\r')) {
				line[--length] = '\0';
			}
			if (length == 0)
				name.clear();
			else if (strncmp(line, "Package: ", 9) == 0)
				name = line + 9;
			else if (strncmp(line, "Status: ", 8) == 0 && !name.empty()
					&& length > 10
					&& strcmp(line + length - 10, " installed") == 0) {
				installed->insert(name);
			}
		}
		free(line);
		fclose(file);
	}

	BMessage reply(kStoreMsgInstalledLoaded);
	reply.AddPointer("set", installed);
	reply.AddInt32("seq", seq);
	if (target.SendMessage(&reply) != B_OK)
		delete installed;
	return B_OK;
}


//	#pragma mark - StoreWindow


StoreWindow::StoreWindow(BRect frame, bool dryRun)
	:
	BWindow(frame, B_TRANSLATE("Store"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fWebView(NULL),
	fWorker(NULL),
	fFetcher(NULL),
	fCatalog(),
	fSection("app"),
	fSelectedCategory("All"),
	fLimit(kPageSize),
	fStateSeq(0),
	fBusyPercent(-1),
	fBusyCancel(kCancelNone),
	fDryRun(dryRun)
{
	fWorker = new PackageWorker(BMessenger(this));
	fWorker->Run();

	fWebView = new BWebView(Bounds(), "store");
	fWebView->SetTarget(BMessenger(this));
	fWebView->AddScriptMessageHandler("store");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fWebView);

	fFetcher = new IconFetcher(BMessenger(this));
	fFetcher->Run();

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

	if (fFetcher != NULL) {
		thread_id thread = fFetcher->Thread();
		fFetcher->Cancel();
		if (fFetcher->Lock())
			fFetcher->Quit();
		status_t exitValue;
		wait_for_thread(thread, &exitValue);
	}
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
				fLastState = "";
				_ReloadStates();
			}
			break;
		}

		case kStoreMsgIconsFetched:
			_PushState();
			break;

		case kStoreMsgInstalledLoaded:
		{
			void* pointer = NULL;
			if (message->FindPointer("set", &pointer) != B_OK
				|| pointer == NULL) {
				break;
			}
			std::set<std::string>* set = (std::set<std::string>*)pointer;
			int32 seq = 0;
			message->FindInt32("seq", &seq);
			if (seq == fStateSeq) {
				fInstalled.swap(*set);
				_PushState();
			}
			delete set;
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
			fBusyCancel = kCancelAvailable;
			fWorker->PostMessage(&fPendingApply);
			_PushState();
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

		case kMsgCancel:
			// Takes effect between apt steps; a running step is never killed.
			if (fBusyCancel == kCancelAvailable) {
				fBusyCancel = kCancelRequested;
				fWorker->PostMessage(kMsgCancel);
				fBusyStatus = B_TRANSLATE("Cancelling after the current step");
				_PushState();
			}
			break;

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

			if (error == kAptErrorInterrupted) {
				_SetStatus(B_TRANSLATE("Cancelled"));
				_FinishBusy();
				break;
			}

			BString text = PackageErrorText(error);
			// The page gets the headline; the details go in the alert.
			_SetStatus(text);
			if (detail[0] != '\0')
				text << "\n\n" << detail;

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
					BString temp(cache.Path());
					temp << ".part";
					BFile file(temp.String(),
						B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
					if (file.InitCheck() == B_OK
						&& file.Write(json.String(), json.Length())
							== (ssize_t)json.Length()) {
						file.Sync();
						BEntry part(temp.String());
						part.Rename(cache.Path(), true);
					}
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
		fLastState = "";
		_PushState();
	} else if (action == "section") {
		if (argument == "app" || argument == "component"
			|| argument == "data") {
			fSection = argument;
			fSelectedCategory = "All";
			fInfoEntryId = "";
			fLimit = kPageSize;
			_PushState();
		}
	} else if (action == "info") {
		fInfoEntryId = argument;
		_PushState();
	} else if (action == "select") {
		fSelectedCategory = argument;
		fInfoEntryId = "";
		fLimit = kPageSize;
		_PushState();
	} else if (action == "search") {
		argument.Trim();
		argument.ToLower();
		if (argument != fQuery) {
			fQuery = argument;
			fInfoEntryId = "";
			fLimit = kPageSize;
			_PushState();
		}
	} else if (action == "more") {
		fLimit += kPageSize;
		_PushState();
	} else if (action == "cancel") {
		PostMessage(kMsgCancel);
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
	BMessage* request = new BMessage();
	request->AddMessenger("target", BMessenger(this));
	request->AddInt32("seq", ++fStateSeq);

	thread_id thread = spawn_thread(&_InstalledThread, "store installed",
		B_LOW_PRIORITY, request);
	if (thread >= 0)
		resume_thread(thread);
	else {
		delete request;
		_PushState();
	}
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
	fBusyCancel = kCancelNone;
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
	fBusyCancel = kCancelNone;
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
		if (fInstalled.count(entry->fPackages.ItemAt(i)->String()) == 0)
			return false;
	}
	return true;
}


bool
StoreWindow::_Matches(StoreEntry* entry) const
{
	// One section per kind; within the apps section the category
	// filter applies.
	if (entry->fKind != fSection)
		return false;
	if (fSection == "app" && fSelectedCategory != "All"
		&& fSelectedCategory != _categoryLabel(entry->fCategory)) {
		return false;
	}
	if (fQuery.IsEmpty())
		return true;

	const char* query = fQuery.String();
	if (strcasestr(entry->fName.String(), query) != NULL
		|| strcasestr(entry->fSummary.String(), query) != NULL
		|| strcasestr(_categoryLabel(entry->fCategory), query) != NULL) {
		return true;
	}
	for (int32 i = 0; i < entry->fPackages.CountItems(); i++) {
		if (strcasestr(entry->fPackages.ItemAt(i)->String(), query) != NULL)
			return true;
	}
	return false;
}


// Local cache path of an entry's icon as a file URL; if it is not
// cached yet, the URL to download is added to missing.
BString
StoreWindow::_EntryIconURL(StoreEntry* entry, BMessage& missing)
{
	BString url = _iconURL(entry->fIcon);
	if (url.IsEmpty())
		return BString();

	BPath dir;
	if (_iconCacheDir(dir) != B_OK)
		return BString();
	BPath file(dir.Path(), _iconFileName(url).String());
	BEntry check(file.Path());
	if (!check.Exists()) {
		missing.AddString("url", url.String());
		return BString();
	}

	// Inline: the page has no origin that may read local files.
	BFile icon(file.Path(), B_READ_ONLY);
	off_t size;
	if (icon.GetSize(&size) != B_OK || size <= 0 || size > 256 * 1024)
		return BString();
	BString raw;
	char* buffer = raw.LockBuffer(size);
	ssize_t read = icon.Read(buffer, size);
	raw.UnlockBuffer(read > 0 ? read : 0);
	if (read != size)
		return BString();

	static const char kAlphabet[]
		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const uint8* in = (const uint8*)raw.String();
	BString uri("data:image/png;base64,");
	for (off_t i = 0; i < size; i += 3) {
		uint32 n = in[i] << 16;
		if (i + 1 < size)
			n |= in[i + 1] << 8;
		if (i + 2 < size)
			n |= in[i + 2];
		uri << kAlphabet[(n >> 18) & 63] << kAlphabet[(n >> 12) & 63];
		uri << (i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=');
		uri << (i + 2 < size ? kAlphabet[n & 63] : '=');
	}
	return uri;
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


void
StoreWindow::_AppendEntryJSON(BString& json, StoreEntry* entry,
	BMessage& missing)
{
	BString icon = _EntryIconURL(entry, missing);

	json << "{\"id\":\"" << _jsonEscape(entry->fId) << "\"";
	json << ",\"kind\":\"" << _jsonEscape(entry->fKind) << "\"";
	json << ",\"name\":\"" << _jsonEscape(entry->fName) << "\"";
	json << ",\"summary\":\"" << _jsonEscape(entry->fSummary) << "\"";
	json << ",\"category\":\""
		<< _jsonEscape(_categoryLabel(entry->fCategory)) << "\"";
	json << ",\"installed\":" << (_IsInstalled(entry) ? "true" : "false");
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


// Only the current page of the filtered list goes to the page, plus
// the detail entry; icons missing for those are queued for download.
BString
StoreWindow::_BuildStateJSON(BMessage& missing)
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
			<< ",\"status\":\"" << _jsonEscape(fBusyStatus)
			<< "\",\"cancel\":" << fBusyCancel << "},";
	} else
		json << "\"busy\":null,";

	BString list;
	int32 total = 0;
	int32 sent = 0;
	for (int32 i = 0; i < fCatalog.CountEntries(); i++) {
		StoreEntry* entry = fCatalog.EntryAt(i);
		if (!_Matches(entry))
			continue;
		total++;
		if (sent >= fLimit)
			continue;
		if (sent > 0)
			list << ",";
		sent++;
		_AppendEntryJSON(list, entry, missing);
	}
	json << "\"total\":" << total << ",\"entries\":[" << list << "]";

	json << ",\"detail\":";
	StoreEntry* detail = fInfoEntryId.IsEmpty()
		? NULL : fCatalog.FindById(fInfoEntryId.String());
	if (detail != NULL)
		_AppendEntryJSON(json, detail, missing);
	else
		json << "null";
	json << "}";
	return json;
}


void
StoreWindow::_PushState()
{
	BMessage missing;
	BString state = _BuildStateJSON(missing);

	// Ask for icons only when the set still to fetch changed.
	BString wanted;
	const char* url;
	for (int32 i = 0; missing.FindString("url", i, &url) == B_OK; i++)
		wanted << url << "\n";
	if (wanted != fLastIconRequest) {
		fLastIconRequest = wanted;
		if (!wanted.IsEmpty())
			fFetcher->Fetch(new BMessage(missing));
		else
			fFetcher->Cancel();
	}

	if (state == fLastState)
		return;
	fLastState = state;

	BString script("Store.updateState(");
	script << state << ")";
	_RunScript(script.String());
}
