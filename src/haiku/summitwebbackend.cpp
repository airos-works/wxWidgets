// SummitWebBackend: see SummitWebBackend.h.
// Licence: wxWindows licence.
#include "wx/haiku/private/summitwebbackend.h"

#include <Application.h>
#include <Handler.h>
#include <Looper.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <PopUpMenu.h>
#include <View.h>
#include <Window.h>

#include <WebKit/WebKitContext.h>
#include <WebKit/WebKitEmbedding.h>
#include <WebKit/WebKitInfo.h>
#include <WebKit/WebKitView.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace summitweb {

namespace {

enum : uint32 {
	kRunTask = 'swRT',
	kMenuBack = 'swMb',
	kMenuForward = 'swMf',
	kMenuReload = 'swMr',
	kMenuEdit = 'swMe',
	kMenuOpenLink = 'swMl'
};

// The handler the commit beacon talks to, and the scheme LoadHTML() serves
// its documents from. Pages see both; neither grants them anything.
const char* const kInternalHandler = "__summitwx";
const char* const kPageScheme = "summit-wxpage";

// At document start in the main frame: the document exists, so the
// navigation was committed (wxEVT_WEBVIEW_NAVIGATED). The engine reports
// loads through state snapshots that do not say when that happened.
const char* const kBeaconScript =
	"(function(){try{window.webkit.messageHandlers.__summitwx.postMessage('commit');}"
	"catch(e){}})();";

// --- work on the BApplication thread ------------------------------------

struct Task {
	std::function<void()> fn;
	sem_id done = -1;
	std::atomic<int> state { 0 };   // 0 waiting, 1 taken by the runner, 2 given up
	~Task()
	{
		if (done >= 0)
			delete_sem(done);
	}
};

class AppRunner : public BHandler {
public:
	AppRunner() : BHandler("summit web runner") {}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != kRunTask) {
			BHandler::MessageReceived(message);
			return;
		}
		void* pointer = nullptr;
		if (message->FindPointer("task", &pointer) != B_OK)
			return;
		std::unique_ptr<std::shared_ptr<Task>> holder(static_cast<std::shared_ptr<Task>*>(pointer));
		Task& task = **holder;
		int expected = 0;
		if (task.state.compare_exchange_strong(expected, 1)) {
			task.fn();
			release_sem(task.done);
		}
	}
};

std::mutex sRunnerLock;
AppRunner* sRunner = nullptr;

std::mutex sEngineLock;
bool sEngineReady = false;

std::mutex sContextLock;
std::map<std::string, std::weak_ptr<BWebKitContext>> sContexts;

std::string
InsertBase(const std::string& html, const std::string& baseURL)
{
	if (baseURL.empty())
		return html;
	std::string escaped;
	for (char c : baseURL) {
		if (c == '"')
			escaped += "&quot;";
		else if (c == '&')
			escaped += "&amp;";
		else if (c == '<')
			escaped += "&lt;";
		else
			escaped += c;
	}
	std::string base = "<base href=\"" + escaped + "\">";
	// After <head ...> when there is one, so the doctype stays first.
	std::string lower(html.size() < 4096 ? html : html.substr(0, 4096));
	for (char& c : lower)
		c = tolower(static_cast<unsigned char>(c));
	for (const char* tag : { "<head", "<html" }) {
		size_t at = lower.find(tag);
		if (at == std::string::npos)
			continue;
		size_t end = lower.find('>', at);
		if (end == std::string::npos)
			continue;
		std::string result = html;
		result.insert(end + 1, base);
		return result;
	}
	size_t doctype = lower.find("<!doctype");
	if (doctype != std::string::npos) {
		size_t end = lower.find('>', doctype);
		if (end != std::string::npos) {
			std::string result = html;
			result.insert(end + 1, base);
			return result;
		}
	}
	return base + html;
}

// The engine's error domains are WebKit's; codes of the network domain are
// libcurl's (the engine's network process uses curl).
ErrorClass
ClassifyError(const std::string& domain, int32 code, bool certificate)
{
	if (certificate)
		return ErrorClass::Certificate;
	if (domain.find("Policy") != std::string::npos)
		return ErrorClass::Security;
	if (domain.find("curl") != std::string::npos || domain.find("Curl") != std::string::npos
		|| domain.find("CURL") != std::string::npos) {
		switch (code) {
			case 1:    // CURLE_UNSUPPORTED_PROTOCOL
			case 3:    // CURLE_URL_MALFORMAT
				return ErrorClass::Request;
			case 5: case 6: case 7:   // proxy, host, connect
			case 28:   // timeout
			case 52: case 55: case 56:
				return ErrorClass::Connection;
			case 35: case 51: case 53: case 54: case 58: case 59: case 60:
			case 77: case 82: case 83: case 90: case 91:
				return ErrorClass::Certificate;
			case 37:   // CURLE_FILE_COULDNT_READ_FILE
			case 78:   // CURLE_REMOTE_FILE_NOT_FOUND
				return ErrorClass::NotFound;
			case 67:   // login denied
				return ErrorClass::Auth;
			case 42:   // aborted by callback
				return ErrorClass::UserCancelled;
		}
		return ErrorClass::Connection;
	}
	return ErrorClass::Other;
}

// window.<name>.postMessage(value), as wx's WebKit backends offer it. A posted
// string arrives as it is. Anything else is turned into JSON text in the page:
// the engine's own conversion sorts the keys of objects and drops null
// members, and BambuStudio's pages post objects that C++ parses as JSON.
std::string
AliasScript(const std::string& name)
{
	return "(function(){var h=window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers."
		+ name + ";if(!h)return;window." + name + "={postMessage:function(m){"
		"var s=(typeof m==='string')?m:JSON.stringify(m);"
		"return h.postMessage(s===undefined?'undefined':s);}};})();";
}

// source as a JavaScript string literal.
std::string
Quote(const std::string& source)
{
	std::string quoted = "\"";
	quoted.reserve(source.size() + 16);
	for (size_t index = 0; index < source.size(); index++) {
		unsigned char c = source[index];
		switch (c) {
			case '\\': quoted += "\\\\"; break;
			case '"': quoted += "\\\""; break;
			case '\n': quoted += "\\n"; break;
			case '\r': quoted += "\\r"; break;
			case '\t': quoted += "\\t"; break;
			case '\b': quoted += "\\b"; break;
			case '\f': quoted += "\\f"; break;
			case '\v': quoted += "\\v"; break;
			default:
				// U+2028 and U+2029 end a line inside a string literal.
				if (c == 0xe2 && index + 2 < source.size() && static_cast<unsigned char>(source[index + 1]) == 0x80
					&& (static_cast<unsigned char>(source[index + 2]) == 0xa8
						|| static_cast<unsigned char>(source[index + 2]) == 0xa9)) {
					quoted += static_cast<unsigned char>(source[index + 2]) == 0xa8 ? "\\u2028" : "\\u2029";
					index += 2;
				} else if (c < 0x20) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					quoted += buffer;
				} else
					quoted += static_cast<char>(c);
		}
	}
	quoted += '"';
	return quoted;
}

bool
ValidName(const std::string& name)
{
	if (name.empty())
		return false;
	for (char c : name) {
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$')
			return false;
	}
	return !isdigit(static_cast<unsigned char>(name[0]));
}

}  // namespace

// --- Impl ---------------------------------------------------------------

class Listener;

struct SummitWebBackend::Impl {
	EventSink sink;

	mutable std::mutex lock;
	std::condition_variable resultsChanged;

	// Set in Create(), cleared in Destroy(); both under `lock`. The view
	// itself is only used with a reference taken under `lock` and, for BView
	// calls, with its looper locked.
	BWebKitView* view = nullptr;
	BView* parent = nullptr;
	Listener* listener = nullptr;
	BMessenger listenerTarget;
	std::shared_ptr<BWebKitContext> context;
	bool destroyed = false;

	State state;
	std::string userAgent;
	std::atomic<bool> contextMenu { false };
	std::atomic<uint64> nextRequest { 1 };
	std::atomic<uint64> vetoedGeneration { 0 };
	// What this class itself asked the engine to load last and has not seen
	// start yet (under `lock`): its address is known before the engine's
	// snapshots say it. kind: 0 nothing, 1 LoadURL/LoadHTML, 2 Reload.
	int requestKind = 0;
	std::string requestURL;

	std::map<uint64, ScriptResultMode> pendingScripts;
	std::map<uint64, ScriptResult> heldResults;
	struct UserScript {
		std::string source;
		bool atStart;
		bool allFrames;
	};
	std::vector<std::string> handlers;
	std::set<std::string> schemes;
	// LoadHTML(): the one document the page scheme serves, and what the
	// application thinks its address is.
	std::string pageURL, pageHTML, pageBase;
	uint64 pageSerial = 0;

	// Listener thread only.
	uint64 generation = 0;
	std::string outcome;
	bool navigatedSent = true;
	bool sawLoading = false;
	// A load started by the page is first reported while the engine still
	// names the old address: Navigating waits for the next snapshot then.
	bool navigatingPending = false;
	std::string committedURL;
	std::string loadURL;
	std::string lastTitle;
	BMessage menuContext;

	void Emit(Event&& event)
	{
		if (sink)
			sink(std::move(event));
	}

	std::string PublicURL(const std::string& url) const
	{
		// lock held
		if (!pageURL.empty() && url == pageURL)
			return pageBase;
		return url;
	}

	void InstallInternals(BWebKitView* target)
	{
		target->AddUserScript(kBeaconScript, true, false);
		std::lock_guard<std::mutex> guard(lock);
		for (const auto& name : handlers)
			target->AddUserScript(AliasScript(name).c_str(), true, true);
	}

	void EmitNavigating(const std::string& url)
	{
		navigatingPending = false;
		loadURL = url;
		Event event;
		event.type = EventType::Navigating;
		event.id = generation;
		event.url = url;
		event.flag = true;
		Emit(std::move(event));
	}

	void EmitNavigated(const std::string& url)
	{
		navigatedSent = true;
		committedURL = url;
		Event event;
		event.type = EventType::Navigated;
		event.id = generation;
		event.url = url;
		Emit(std::move(event));
	}

	void FinishScript(uint64 id, bool ok, const std::string& value);
	void FailPendingScripts(const std::string& why);

	void HandleState(BMessage* message);
	void HandleScriptMessage(BMessage* message);
	void HandleScriptResult(BMessage* message);
	void HandleSchemeRequest(BMessage* message);
	void HandleContextMenu(BMessage* message);
	void HandleMessage(BMessage* message);
};

class Listener : public BLooper {
public:
	explicit Listener(SummitWebBackend::Impl* impl)
		:
		// The engine sends with a zero timeout and drops what does not fit:
		// a roomy port, and nothing slow in MessageReceived().
		BLooper("summit web listener", B_NORMAL_PRIORITY, 2048),
		fImpl(impl)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		fImpl->HandleMessage(message);
	}

private:
	SummitWebBackend::Impl* fImpl;
};

void
SummitWebBackend::Impl::FinishScript(uint64 id, bool ok, const std::string& value)
{
	ScriptResultMode mode = ScriptResultMode::Discard;
	{
		std::lock_guard<std::mutex> guard(lock);
		auto found = pendingScripts.find(id);
		if (found == pendingScripts.end())
			return;
		mode = found->second;
		pendingScripts.erase(found);
		if (mode == ScriptResultMode::Hold) {
			ScriptResult& result = heldResults[id];
			result.ok = ok;
			result.value = value;
		}
	}
	if (mode == ScriptResultMode::Hold)
		resultsChanged.notify_all();
	else if (mode == ScriptResultMode::Event) {
		Event event;
		event.type = EventType::ScriptResult;
		event.id = id;
		event.ok = ok;
		event.text = value;
		Emit(std::move(event));
	}
}

void
SummitWebBackend::Impl::FailPendingScripts(const std::string& why)
{
	std::vector<uint64> ids;
	{
		std::lock_guard<std::mutex> guard(lock);
		for (const auto& entry : pendingScripts)
			ids.push_back(entry.first);
	}
	for (uint64 id : ids)
		FinishScript(id, false, why);
}

void
SummitWebBackend::Impl::HandleState(BMessage* message)
{
	std::string url = message->GetString("url", "");
	std::string title = message->GetString("title", "");
	uint64 loadGeneration = message->GetUInt64("loadGeneration", 0);
	std::string loadOutcome = message->GetString("loadOutcome", "idle");
	bool titleChanged = false;
	{
		std::lock_guard<std::mutex> guard(lock);
		url = PublicURL(url);
		state.url = url;
		state.title = title;
		state.loading = message->GetBool("loading", false);
		state.canGoBack = message->GetBool("canGoBack", false);
		state.canGoForward = message->GetBool("canGoForward", false);
		state.progress = message->GetDouble("progress", 0);
		state.pageZoom = message->GetDouble("pageZoom", 1);
		state.softwareRendering = message->GetBool("softwareRendering", false);
		state.webProcess = message->GetInt32("processID", state.webProcess);
		if (loadOutcome == "loading")
			state.processGone = false;
	}

	if (loadGeneration != generation) {
		// Another load. One that is first seen finished went nowhere: a
		// fragment or history.pushState() navigation inside the document,
		// which wx's other backends do not announce either.
		generation = loadGeneration;
		outcome.clear();
		sawLoading = false;
		navigatedSent = true;
		navigatingPending = false;
		if (loadOutcome == "loading") {
			sawLoading = true;
			navigatedSent = false;
			// The engine's first snapshots of a load the page started (a
			// link, location.href, history) still name the document's own
			// address; Navigating waits for the snapshot that has the new
			// one. What LoadURL() and Reload() asked for is known at once.
			int kind;
			std::string requested;
			{
				std::lock_guard<std::mutex> guard(lock);
				kind = requestKind;
				requested = PublicURL(requestURL);
				requestKind = 0;
				requestURL.clear();
			}
			if (kind == 1)
				EmitNavigating(requested.empty() ? url : requested);
			else if (kind == 2 || url != committedURL)
				EmitNavigating(url);
			else
				navigatingPending = true;
		}
	} else if (navigatingPending && loadOutcome == "loading" && url != committedURL)
		EmitNavigating(url);
	if (navigatingPending && loadOutcome != "loading") {
		// It ended before a second snapshot came.
		std::string target = loadOutcome == "failed" ? message->GetString("loadErrorURL", url.c_str())
			: loadOutcome == "succeeded" ? message->GetString("loadSuccessURL", url.c_str()) : url;
		EmitNavigating(target);
	}
	if (loadOutcome != outcome) {
		outcome = loadOutcome;
		const bool vetoed = vetoedGeneration.load() == generation;
		if (!sawLoading) {
			// nothing was announced, so nothing ends
		} else if (outcome == "succeeded") {
			std::string loaded = message->GetString("loadSuccessURL", url.c_str());
			{
				std::lock_guard<std::mutex> guard(lock);
				loaded = PublicURL(loaded);
			}
			if (!navigatedSent) {
				// No beacon: a document user scripts do not run in, or one
				// restored from the back/forward cache.
				EmitNavigated(loaded);
			}
			committedURL = loaded;
			Event event;
			event.type = EventType::Loaded;
			event.id = generation;
			event.url = loaded;
			Emit(std::move(event));
			sawLoading = false;
		} else if (outcome == "failed") {
			Event event;
			event.type = EventType::Error;
			event.id = generation;
			event.url = message->GetString("loadErrorURL", url.c_str());
			event.text = message->GetString("loadErrorDescription", "");
			event.name = message->GetString("loadErrorDomain", "");
			event.code = message->GetInt32("loadErrorCode", 0);
			event.errorClass = ClassifyError(event.name, event.code,
				message->GetBool("loadErrorCertificate", false));
			Emit(std::move(event));
			sawLoading = false;
		} else if (outcome == "cancelled") {
			if (!vetoed) {
				Event event;
				event.type = EventType::Error;
				event.id = generation;
				event.url = loadURL.empty() ? url : loadURL;
				event.text = "The load was cancelled";
				event.errorClass = ErrorClass::UserCancelled;
				Emit(std::move(event));
			}
			sawLoading = false;
		}
	}
	if (title != lastTitle) {
		lastTitle = title;
		titleChanged = true;
	}
	if (titleChanged) {
		Event event;
		event.type = EventType::TitleChanged;
		event.text = title;
		event.url = url;
		Emit(std::move(event));
	}
}

void
SummitWebBackend::Impl::HandleScriptMessage(BMessage* message)
{
	// postMessage() returns a promise that stays pending until answered.
	uint64 token = message->GetUInt64("reply_token", 0);
	if (token)
		BWebKitView::ReplyToScriptMessage(token, "");
	std::string name = message->GetString("name", "");
	bool mainFrame = message->GetBool("main_frame", true);
	if (name == kInternalHandler) {
		if (mainFrame && !navigatedSent && sawLoading
			&& !strcmp(message->GetString("body", ""), "commit")) {
			std::string url = message->GetString("frame_url", "");
			{
				std::lock_guard<std::mutex> guard(lock);
				url = PublicURL(url);
			}
			if (navigatingPending)
				EmitNavigating(url);
			EmitNavigated(url);
		}
		return;
	}
	Event event;
	event.type = EventType::ScriptMessage;
	event.name = name;
	event.text = message->GetString("body", "");
	event.url = message->GetString("frame_url", "");
	event.mainFrame = mainFrame;
	Emit(std::move(event));
}

void
SummitWebBackend::Impl::HandleScriptResult(BMessage* message)
{
	uint64 id = message->GetUInt64("identifier", 0);
	const char* error = nullptr;
	if (message->FindString("error", &error) == B_OK) {
		FinishScript(id, false, error);
		return;
	}
	// "string": the value itself; "json": numbers, booleans, null, objects
	// and arrays as JSON text, which is how wx reports them; "undefined".
	std::string type = message->GetString("result_type", "undefined");
	if (type == "undefined")
		FinishScript(id, true, "undefined");
	else
		FinishScript(id, true, message->GetString("result", ""));
}

void
SummitWebBackend::Impl::HandleSchemeRequest(BMessage* message)
{
	uint64 id = message->GetUInt64("identifier", 0);
	std::string scheme = message->GetString("scheme", "");
	std::string url = message->GetString("url", "");
	if (scheme == kPageScheme) {
		std::string html;
		bool found = false;
		{
			std::lock_guard<std::mutex> guard(lock);
			if (url == pageURL) {
				html = pageHTML;
				found = true;
			}
		}
		if (found) {
			BWebKitView::RespondToURLSchemeRequest(id, 200, "text/html; charset=utf-8", nullptr,
				html.data(), html.size());
		} else
			BWebKitView::FailURLSchemeRequest(id, "No such page");
		return;
	}
	Event event;
	event.type = EventType::SchemeRequest;
	event.id = id;
	event.url = url;
	event.name = scheme;
	event.method = message->GetString("method", "GET");
	event.mainFrame = message->GetBool("main_frame", false);
	const char* name;
	const char* value;
	for (int32 index = 0; message->FindString("header_name", index, &name) == B_OK
		&& message->FindString("header_value", index, &value) == B_OK; index++)
		event.headers.emplace_back(name, value);
	const void* data;
	ssize_t size;
	if (message->FindData("body", B_RAW_TYPE, &data, &size) == B_OK && size > 0)
		event.body.assign(static_cast<const char*>(data), size);
	Emit(std::move(event));
}

void
SummitWebBackend::Impl::HandleContextMenu(BMessage* message)
{
	if (!contextMenu.load())
		return;
	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return;
	State current;
	BWebKitView* target;
	{
		std::lock_guard<std::mutex> guard(lock);
		current = state;
		target = view;
	}
	if (!target || target->LockLooperWithTimeout(200000) != B_OK)
		return;
	BPoint screen = target->ConvertToScreen(where);
	target->UnlockLooper();

	menuContext = *message;
	BPopUpMenu* menu = new BPopUpMenu("page", false, false);
	const char* link = message->GetString("link_url", "");
	if (*link) {
		BMessage* open = new BMessage(kMenuOpenLink);
		open->AddString("url", link);
		menu->AddItem(new BMenuItem("Open link in browser", open));
		menu->AddSeparatorItem();
	}
	BMenuItem* item = new BMenuItem("Back", new BMessage(kMenuBack));
	item->SetEnabled(current.canGoBack);
	menu->AddItem(item);
	item = new BMenuItem("Forward", new BMessage(kMenuForward));
	item->SetEnabled(current.canGoForward);
	menu->AddItem(item);
	menu->AddItem(new BMenuItem("Reload", new BMessage(kMenuReload)));
	menu->AddSeparatorItem();
	const bool editable = message->GetBool("editable", false);
	const bool selected = message->GetBool("selected", false) || *message->GetString("selected_text", "");
	struct { const char* label; uint32 command; bool enabled; } edits[] = {
		{ "Cut", B_CUT, editable && selected },
		{ "Copy", B_COPY, selected },
		{ "Paste", B_PASTE, editable },
		{ "Select all", B_SELECT_ALL, true }
	};
	for (const auto& edit : edits) {
		BMessage* command = new BMessage(kMenuEdit);
		command->AddUInt32("command", edit.command);
		item = new BMenuItem(edit.label, command);
		item->SetEnabled(edit.enabled);
		menu->AddItem(item);
	}
	menu->SetTargetForItems(listenerTarget);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(screen, true, true, true);
}

void
SummitWebBackend::Impl::HandleMessage(BMessage* message)
{
	switch (message->what) {
		case B_WEBKIT_STATE_CHANGED:
			HandleState(message);
			break;
		case B_WEBKIT_SCRIPT_MESSAGE:
			HandleScriptMessage(message);
			break;
		case B_WEBKIT_JAVASCRIPT_RESULT:
			HandleScriptResult(message);
			break;
		case B_WEBKIT_URL_SCHEME_REQUEST:
			HandleSchemeRequest(message);
			break;
		case B_WEBKIT_URL_SCHEME_REQUEST_STOPPED: {
			Event event;
			event.type = EventType::SchemeRequestStopped;
			event.id = message->GetUInt64("identifier", 0);
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_NEW_PAGE_REQUESTED: {
			// The opener's script waits for the answer.
			BWebKitView::DeclineNewPage(message->GetUInt64("identifier", 0));
			Event event;
			event.type = EventType::NewWindow;
			event.url = message->GetString("url", "");
			event.userGesture = message->GetBool("user_gesture", false);
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_LINK_OPEN_REQUESTED: {
			Event event;
			event.type = EventType::NewWindow;
			event.url = message->GetString("url", "");
			event.userGesture = true;
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_SCHEME_OPEN_REQUESTED: {
			Event event;
			event.type = EventType::Navigating;
			event.url = message->GetString("url", "");
			event.mainFrame = message->GetBool("main_frame", true);
			event.userGesture = message->GetBool("user_gesture", false);
			event.blocked = true;
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_FULLSCREEN_REQUESTED: {
			uint64 request = message->GetUInt64("request", 0);
			bool enter = message->GetBool("fullscreen", false);
			if (request) {
				BWebKitView* target;
				{
					std::lock_guard<std::mutex> guard(lock);
					target = view;
				}
				if (target)
					target->CompleteFullScreenRequest(request, true);
			}
			Event event;
			event.type = EventType::FullscreenChanged;
			event.flag = request ? enter : false;
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_PROCESS_EXITED: {
			const char* reason = nullptr;
			if (message->FindString("reason", &reason) != B_OK)
				break;
			{
				std::lock_guard<std::mutex> guard(lock);
				state.processGone = true;
				state.loading = false;
			}
			FailPendingScripts("The page's web process exited");
			Event event;
			event.type = EventType::ProcessExited;
			event.text = reason;
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_COOKIES: {
			Event event;
			event.type = EventType::Cookies;
			event.id = message->GetUInt64("identifier", 0);
			const char* error = nullptr;
			if (message->FindString("error", &error) == B_OK) {
				event.ok = false;
				event.text = error;
			}
			BMessage entry;
			for (int32 index = 0; message->FindMessage("cookie", index, &entry) == B_OK; index++) {
				Cookie cookie;
				cookie.name = entry.GetString("name", "");
				cookie.value = entry.GetString("value", "");
				cookie.domain = entry.GetString("domain", "");
				cookie.path = entry.GetString("path", "");
				cookie.httpOnly = entry.GetBool("httpOnly", false);
				cookie.secure = entry.GetBool("secure", false);
				cookie.session = entry.GetBool("session", false);
				event.cookies.push_back(std::move(cookie));
			}
			Emit(std::move(event));
			break;
		}
		case B_WEBKIT_CONTEXT_MENU:
			HandleContextMenu(message);
			break;
		case kMenuBack:
		case kMenuForward:
		case kMenuReload:
		case kMenuEdit:
		case kMenuOpenLink: {
			BWebKitView* target;
			{
				std::lock_guard<std::mutex> guard(lock);
				target = view;
			}
			if (!target)
				break;
			if (message->what == kMenuBack)
				target->GoBack();
			else if (message->what == kMenuForward)
				target->GoForward();
			else if (message->what == kMenuReload)
				target->Reload();
			else if (message->what == kMenuEdit)
				target->ExecuteEditCommand(message->GetUInt32("command", B_COPY));
			else {
				Event event;
				event.type = EventType::NewWindow;
				event.url = message->GetString("url", "");
				event.userGesture = true;
				Emit(std::move(event));
			}
			break;
		}
		default:
			break;
	}
}

// --- process-wide ---------------------------------------------------------

status_t
SummitWebBackend::RunOnApplicationThread(const std::function<void()>& fn, bigtime_t timeout)
{
	if (!be_app)
		return B_NO_INIT;
	if (be_app->Thread() == find_thread(nullptr)) {
		fn();
		return B_OK;
	}
	BMessenger target;
	{
		std::lock_guard<std::mutex> guard(sRunnerLock);
		if (!sRunner) {
			// The application is locked by the thread that made it until it
			// runs; that would be a caller's mistake, not a reason to hang.
			if (be_app->LockWithTimeout(timeout) != B_OK)
				return B_TIMED_OUT;
			sRunner = new AppRunner;
			be_app->AddHandler(sRunner);
			be_app->Unlock();
		}
		target = BMessenger(sRunner);
	}
	auto task = std::make_shared<Task>();
	task->fn = fn;
	task->done = create_sem(0, "summit web task");
	if (task->done < 0)
		return task->done;
	BMessage message(kRunTask);
	message.AddPointer("task", new std::shared_ptr<Task>(task));
	status_t status = target.SendMessage(&message, static_cast<BHandler*>(nullptr), timeout);
	if (status != B_OK)
		return status;   // the holder leaks; the application is not taking messages
	do {
		status = acquire_sem_etc(task->done, 1, B_RELATIVE_TIMEOUT, timeout);
	} while (status == B_INTERRUPTED);
	if (status == B_OK)
		return B_OK;
	int expected = 0;
	if (task->state.compare_exchange_strong(expected, 2))
		return B_TIMED_OUT;   // never started, and now never will
	// It is running: fn refers to the caller's stack, so see it through.
	do {
		status = acquire_sem(task->done);
	} while (status == B_INTERRUPTED);
	return status;
}

status_t
SummitWebBackend::InitializeEngine()
{
	std::lock_guard<std::mutex> guard(sEngineLock);
	if (sEngineReady)
		return B_OK;
	status_t result = B_ERROR;
	status_t status = RunOnApplicationThread([&result] { result = BWebKitInitialize(); });
	if (status != B_OK)
		return status;
	sEngineReady = result == B_OK;
	return result;
}

bool
SummitWebBackend::EngineAvailable()
{
	return InitializeEngine() == B_OK;
}

std::string
SummitWebBackend::EngineVersion()
{
	std::string version = "WebKit ";
	version += BWebKitVersion();
	version += " (summit_webkit ";
	version += BWebKitPortVersion();
	version += ", ";
	version += BWebKitSourceRevision();
	version += ")";
	return version;
}

std::string
SummitWebBackend::EngineVersionNumber()
{
	return BWebKitVersion();
}

static std::shared_ptr<BWebKitContext>
AcquireContext(const Options& options, status_t& status)
{
	std::string key = options.privateSession ? std::string("\x01private") : options.profilePath;
	std::lock_guard<std::mutex> guard(sContextLock);
	if (auto existing = sContexts[key].lock()) {
		status = existing->InitCheck();
		return existing;
	}
	std::shared_ptr<BWebKitContext> context;
	status_t run = SummitWebBackend::RunOnApplicationThread([&] {
		context = std::make_shared<BWebKitContext>(
			options.privateSession || options.profilePath.empty() ? nullptr : options.profilePath.c_str(),
			options.privateSession);
	});
	if (run != B_OK) {
		status = run;
		return nullptr;
	}
	status = context->InitCheck();
	if (status != B_OK) {
		SummitWebBackend::RunOnApplicationThread([&] { context.reset(); });
		return nullptr;
	}
	sContexts[key] = context;
	return context;
}

// --- SummitWebBackend ------------------------------------------------------

SummitWebBackend::SummitWebBackend(EventSink sink)
	:
	fImpl(std::make_shared<Impl>())
{
	fImpl->sink = std::move(sink);
}

SummitWebBackend::~SummitWebBackend()
{
	Destroy();
}

status_t
SummitWebBackend::Create(BView* parent, BRect frame, const Options& options)
{
	if (!parent)
		return B_BAD_VALUE;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		if (fImpl->view)
			return B_NOT_ALLOWED;
	}
	status_t status = InitializeEngine();
	if (status != B_OK)
		return status;
	std::shared_ptr<BWebKitContext> context = AcquireContext(options, status);
	if (!context)
		return status != B_OK ? status : B_ERROR;

	Listener* listener = new Listener(fImpl.get());
	listener->Run();
	BMessenger target(listener);

	BMessage settings;
	if (!options.userAgent.empty())
		settings.AddString("userAgent", options.userAgent.c_str());
	BWebKitView* view = new BWebKitView(frame, "summit web view", target, settings,
		options.resizingMode, context);
	status = view->InitCheck();
	if (status != B_OK) {
		delete view;
		listener->Lock();
		listener->Quit();
		RunOnApplicationThread([&] { context.reset(); });
		return status;
	}
	view->AddScriptMessageHandler(kInternalHandler, target);
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		fImpl->view = view;
		fImpl->parent = parent;
		fImpl->listener = listener;
		fImpl->listenerTarget = target;
		fImpl->context = std::move(context);
		fImpl->userAgent = options.userAgent;
		fImpl->destroyed = false;
	}
	fImpl->InstallInternals(view);
	if (options.hidden)
		view->Hide();
	const bool locked = parent->LockLooper();
	parent->AddChild(view);
	if (locked)
		parent->UnlockLooper();
	return B_OK;
}

void
SummitWebBackend::Destroy()
{
	BWebKitView* view;
	Listener* listener;
	std::shared_ptr<BWebKitContext> context;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		view = fImpl->view;
		listener = fImpl->listener;
		context = std::move(fImpl->context);
		fImpl->view = nullptr;
		fImpl->parent = nullptr;
		fImpl->listener = nullptr;
		fImpl->listenerTarget = BMessenger();
		fImpl->destroyed = true;
	}
	if (listener) {
		// First, so that nothing uses the view while it goes. From another
		// thread Quit() returns when the listener has ended: after this the
		// sink is not called again.
		listener->Lock();
		listener->Quit();
	}
	if (view) {
		const bool locked = view->LockLooper();
		BLooper* looper = locked ? view->Looper() : nullptr;
		view->RemoveSelf();
		if (looper)
			looper->Unlock();
		delete view;
	}
	fImpl->FailPendingScripts("The web view was destroyed");
	fImpl->resultsChanged.notify_all();
	if (context) {
		// The last reference must go on the engine's thread.
		RunOnApplicationThread([&] { context.reset(); });
	}
}

bool
SummitWebBackend::IsCreated() const
{
	std::lock_guard<std::mutex> guard(fImpl->lock);
	return fImpl->view != nullptr;
}

BView*
SummitWebBackend::View() const
{
	std::lock_guard<std::mutex> guard(fImpl->lock);
	return fImpl->view;
}

// The view pointer, for calls the engine queues itself (every BWebKitView
// method used below posts to the engine's thread and returns).
static BWebKitView*
ViewOf(const std::shared_ptr<SummitWebBackend::Impl>& impl)
{
	std::lock_guard<std::mutex> guard(impl->lock);
	return impl->view;
}

void
SummitWebBackend::SetFrame(BRect frame)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view)
		return;
	const bool locked = view->LockLooper();
	view->MoveTo(frame.LeftTop());
	view->ResizeTo(frame.Width(), frame.Height());
	if (locked)
		view->UnlockLooper();
}

void
SummitWebBackend::Show(bool show)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view)
		return;
	const bool locked = view->LockLooper();
	// BView counts Show()/Hide() calls; keep it to one level.
	if (show && view->IsHidden(view))
		view->Show();
	else if (!show && !view->IsHidden(view))
		view->Hide();
	if (locked)
		view->UnlockLooper();
}

void
SummitWebBackend::SetFocus()
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view)
		return;
	if (view->LockLooper()) {
		view->MakeFocus(true);
		view->UnlockLooper();
	}
}

void
SummitWebBackend::LoadURL(const std::string& url)
{
	if (BWebKitView* view = ViewOf(fImpl)) {
		{
			std::lock_guard<std::mutex> guard(fImpl->lock);
			fImpl->requestKind = 1;
			fImpl->requestURL = url;
		}
		view->LoadURL(url.c_str());
	}
}

void
SummitWebBackend::LoadHTML(const std::string& html, const std::string& baseURL)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view)
		return;
	std::string url;
	bool registerScheme = false;
	BMessenger target;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		registerScheme = fImpl->schemes.insert(kPageScheme).second;
		target = fImpl->listenerTarget;
		url = std::string(kPageScheme) + "://page/" + std::to_string(++fImpl->pageSerial);
		fImpl->pageURL = url;
		fImpl->pageHTML = InsertBase(html, baseURL);
		fImpl->pageBase = baseURL.empty() ? std::string("about:blank") : baseURL;
		fImpl->requestKind = 1;
		fImpl->requestURL = url;
	}
	if (registerScheme) {
		// Local, so the document may use file: resources as a file would.
		view->SetURLSchemeHandler(kPageScheme, target, B_WEBKIT_SCHEME_LOCAL | B_WEBKIT_SCHEME_SECURE);
	}
	view->LoadURL(url.c_str());
}

void
SummitWebBackend::Reload(bool bypassCache)
{
	if (BWebKitView* view = ViewOf(fImpl)) {
		{
			std::lock_guard<std::mutex> guard(fImpl->lock);
			fImpl->requestKind = 2;
			fImpl->requestURL.clear();
		}
		if (bypassCache)
			view->ReloadFromOrigin();
		else
			view->Reload();
	}
}

void
SummitWebBackend::Stop()
{
	if (BWebKitView* view = ViewOf(fImpl))
		view->Stop();
}

void
SummitWebBackend::GoBack()
{
	if (BWebKitView* view = ViewOf(fImpl))
		view->GoBack();
}

void
SummitWebBackend::GoForward()
{
	if (BWebKitView* view = ViewOf(fImpl))
		view->GoForward();
}

void
SummitWebBackend::Veto(uint64 generation)
{
	fImpl->vetoedGeneration.store(generation);
	Stop();
}

State
SummitWebBackend::GetState() const
{
	std::lock_guard<std::mutex> guard(fImpl->lock);
	return fImpl->state;
}

void
SummitWebBackend::SetUserAgent(const std::string& userAgent)
{
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		fImpl->userAgent = userAgent;
	}
	if (BWebKitView* view = ViewOf(fImpl))
		view->SetCustomUserAgent(userAgent.c_str());
}

std::string
SummitWebBackend::UserAgent() const
{
	std::lock_guard<std::mutex> guard(fImpl->lock);
	return fImpl->userAgent;
}

void
SummitWebBackend::SetZoomFactor(double factor)
{
	if (BWebKitView* view = ViewOf(fImpl)) {
		view->SetZoomFactor(factor);
		std::lock_guard<std::mutex> guard(fImpl->lock);
		fImpl->state.pageZoom = factor;
	}
}

void
SummitWebBackend::EnableContextMenu(bool enable)
{
	fImpl->contextMenu.store(enable);
}

bool
SummitWebBackend::IsContextMenuEnabled() const
{
	return fImpl->contextMenu.load();
}

void
SummitWebBackend::AddUserScript(const std::string& source, bool atDocumentStart, bool allFrames)
{
	if (BWebKitView* view = ViewOf(fImpl))
		view->AddUserScript(source.c_str(), atDocumentStart, allFrames);
}

void
SummitWebBackend::RemoveAllUserScripts()
{
	if (BWebKitView* view = ViewOf(fImpl)) {
		// The engine has one list; put this class's own scripts back.
		view->RemoveAllUserScripts();
		fImpl->InstallInternals(view);
	}
}

bool
SummitWebBackend::AddScriptMessageHandler(const std::string& name)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view || !ValidName(name) || name == kInternalHandler)
		return false;
	BMessenger target;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		for (const auto& existing : fImpl->handlers) {
			if (existing == name)
				return true;
		}
		fImpl->handlers.push_back(name);
		target = fImpl->listenerTarget;
	}
	view->AddScriptMessageHandler(name.c_str(), target);
	// window.<name>, for documents made from now on and for the current one.
	std::string alias = AliasScript(name);
	view->AddUserScript(alias.c_str(), true, true);
	RunScript("try{" + alias + "}catch(e){}");
	return true;
}

bool
SummitWebBackend::RemoveScriptMessageHandler(const std::string& name)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view)
		return false;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		auto& handlers = fImpl->handlers;
		bool found = false;
		for (auto it = handlers.begin(); it != handlers.end(); ++it) {
			if (*it == name) {
				handlers.erase(it);
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	view->RemoveScriptMessageHandler(name.c_str());
	return true;
}

uint64
SummitWebBackend::RunScript(const std::string& source, ScriptResultMode mode)
{
	BWebKitView* view;
	BMessenger target;
	uint64 id = fImpl->nextRequest.fetch_add(1);
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		view = fImpl->view;
		if (!view)
			return 0;
		target = fImpl->listenerTarget;
		fImpl->pendingScripts[id] = mode;
	}
	view->EvaluateJavaScript(source.c_str(), target, id, false);
	return id;
}

bool
SummitWebBackend::WaitScriptResult(uint64 id, ScriptResult& result, bigtime_t timeout)
{
	std::unique_lock<std::mutex> guard(fImpl->lock);
	auto ready = [&] {
		return fImpl->heldResults.count(id) != 0 || fImpl->pendingScripts.count(id) == 0;
	};
	if (!fImpl->resultsChanged.wait_for(guard, std::chrono::microseconds(timeout), ready))
		return false;
	auto found = fImpl->heldResults.find(id);
	if (found == fImpl->heldResults.end()) {
		// Not pending and not held: abandoned, or never asked for.
		result.ok = false;
		result.value = "No such script request";
		return true;
	}
	result = std::move(found->second);
	fImpl->heldResults.erase(found);
	return true;
}

void
SummitWebBackend::AbandonScript(uint64 id)
{
	std::lock_guard<std::mutex> guard(fImpl->lock);
	auto pending = fImpl->pendingScripts.find(id);
	if (pending != fImpl->pendingScripts.end())
		pending->second = ScriptResultMode::Discard;
	fImpl->heldResults.erase(id);
}

bool
SummitWebBackend::RunScriptSync(const std::string& source, std::string* output, bigtime_t timeout,
	const std::function<void()>& pump, bigtime_t slice)
{
	// With a result wanted, the value is turned into text in the page, as wx's
	// own wrapper does it (indirect eval: global scope): strings as they
	// are, objects and arrays as JSON with their members in order. A page
	// whose Content-Security-Policy forbids eval gets the script as it is and
	// the engine's conversion (JSON with sorted keys).
	std::string wrapped;
	if (output) {
		wrapped = "(function(){var r=(0,eval)(" + Quote(source) + ");"
			"if(r===undefined)return 'undefined';if(r===null)return 'null';"
			"if(typeof r==='object'){try{var j=JSON.stringify(r);if(j!==undefined)return j;}catch(e){}}"
			"return String(r);})()";
	}
	const bigtime_t deadline = system_time() + timeout;
	ScriptResult result;
	for (int attempt = 0; attempt < 2; attempt++) {
		uint64 id = RunScript(attempt == 0 && output ? wrapped : source, ScriptResultMode::Hold);
		if (!id) {
			if (output)
				*output = "There is no web view";
			return false;
		}
		for (;;) {
			bigtime_t now = system_time();
			bigtime_t wait = pump ? slice : deadline - now;
			if (now + wait > deadline)
				wait = deadline > now ? deadline - now : 0;
			if (WaitScriptResult(id, result, wait))
				break;
			if (system_time() >= deadline) {
				AbandonScript(id);
				if (output)
					*output = "The script did not finish in time";
				return false;
			}
			if (pump)
				pump();
		}
		if (result.ok || attempt == 1 || !output || result.value.find("EvalError") == std::string::npos)
			break;
	}
	if (output)
		*output = result.value;
	return result.ok;
}

bool
SummitWebBackend::RegisterScheme(const std::string& scheme)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view || scheme.empty())
		return false;
	std::string lower = scheme;
	for (char& c : lower)
		c = tolower(static_cast<unsigned char>(c));
	for (const char* special : { "http", "https", "file", "ftp", "ws", "wss", "about", "data", "blob", "javascript" }) {
		if (lower == special)
			return false;
	}
	BMessenger target;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		if (!fImpl->schemes.insert(lower).second)
			return true;
		target = fImpl->listenerTarget;
	}
	// As wx's handlers are used: pages of local files load from them
	// (wxfs:, memory:), so the scheme is local and CORS-enabled.
	view->SetURLSchemeHandler(lower.c_str(), target,
		B_WEBKIT_SCHEME_LOCAL | B_WEBKIT_SCHEME_CORS_ENABLED | B_WEBKIT_SCHEME_SECURE);
	return true;
}

void
SummitWebBackend::RespondToSchemeRequest(uint64 id, int32 status, const std::string& mimeType,
	const void* data, size_t size, const std::vector<std::pair<std::string, std::string>>& headers)
{
	BMessage list;
	for (const auto& header : headers) {
		list.AddString("name", header.first.c_str());
		list.AddString("value", header.second.c_str());
	}
	BWebKitView::RespondToURLSchemeRequest(id, status, mimeType.empty() ? nullptr : mimeType.c_str(),
		headers.empty() ? nullptr : &list, data, size);
}

void
SummitWebBackend::FailSchemeRequest(uint64 id, const std::string& description)
{
	BWebKitView::FailURLSchemeRequest(id, description.c_str());
}

void
SummitWebBackend::ExecuteEditCommand(EditCommand command)
{
	BWebKitView* view = ViewOf(fImpl);
	if (!view)
		return;
	uint32 what = B_COPY;
	switch (command) {
		case EditCommand::Cut: what = B_CUT; break;
		case EditCommand::Copy: what = B_COPY; break;
		case EditCommand::Paste: what = B_PASTE; break;
		case EditCommand::SelectAll: what = B_SELECT_ALL; break;
		case EditCommand::Undo: what = B_UNDO; break;
		case EditCommand::Redo: what = B_REDO; break;
	}
	view->ExecuteEditCommand(what);
}

uint64
SummitWebBackend::GetCookies(const std::string& url)
{
	BWebKitView* view;
	BMessenger target;
	{
		std::lock_guard<std::mutex> guard(fImpl->lock);
		view = fImpl->view;
		target = fImpl->listenerTarget;
	}
	if (!view)
		return 0;
	uint64 id = fImpl->nextRequest.fetch_add(1);
	view->GetCookies(url.c_str(), target, id);
	return id;
}

void
SummitWebBackend::DeleteCookies(const std::string& url, const std::string& name)
{
	if (BWebKitView* view = ViewOf(fImpl))
		view->DeleteCookies(url.c_str(), name.c_str(), BMessenger(), 0);
}

}  // namespace summitweb
