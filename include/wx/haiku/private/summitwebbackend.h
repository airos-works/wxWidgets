// SummitWebBackend: the summit_webkit engine (WebKit2 for air/OS, BWebKitView)
// behind exactly the operations a wxWebView backend needs.
//
// Licence: wxWindows licence (it is meant to be merged into wxWidgets).
//
// Threading model (made for the native wx port, where wx runs on one thread
// and no wx code may run on a BLooper thread):
//
//  - Every method may be called from any thread that is not the engine's
//    listener thread; normally that is the wx main thread. Methods that touch
//    the BView hierarchy (Create, Destroy, SetFrame, Show, SetFocus) lock the
//    parent's looper themselves; the caller may already hold that lock
//    (BLooper locks nest). No method waits for the engine or for a window
//    thread, except RunScriptSync/WaitScriptResult, which wait with a timeout
//    and never while holding a looper lock taken by this class.
//  - Engine notifications arrive as BMessages on a private BLooper (one per
//    backend, "summit web listener"). That thread turns them into plain
//    summitweb::Event structs and hands them to the EventSink given to the
//    constructor. The sink must be thread-safe and must not block: post the
//    event to the main thread's queue and return.
//  - The engine itself must be initialised on the BApplication thread and
//    website data contexts must be created there; this class posts that work
//    to be_app and waits for it, so be_app must exist and be running (Run()
//    called on whichever thread) before the first Create().
//
// One website data context (cookies, storage, one process pool with one
// network process) is shared by all backends created with the same
// Options::profilePath; see STATUS-webview.md for what that costs.
#ifndef SUMMIT_WEB_BACKEND_H
#define SUMMIT_WEB_BACKEND_H

#include <Rect.h>
#include <SupportDefs.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class BView;

namespace summitweb {

// Same order as wxWebViewNavigationError.
enum class ErrorClass {
	Connection,
	Certificate,
	Auth,
	Security,
	NotFound,
	Request,
	UserCancelled,
	Other
};

enum class EventType {
	// A main-frame load started: url, id (the load's generation). flag is
	// true when Stop() can still keep the page where it is (see Veto()).
	// Also sent, with flag false and blocked true, for URLs of schemes the
	// engine cannot show (mailto:, app:, bambu:...): the engine has already
	// dropped that navigation; mainFrame says where it came from.
	Navigating,
	// The main frame's new document exists (committed): url, id (as above).
	Navigated,
	// The main frame finished loading: url, id.
	Loaded,
	// A main-frame load failed: url, id, text (description), name (the
	// engine's error domain), code (the engine's error code), errorClass.
	// A load that fails at once (no such host) can do so before the engine
	// has said where it was going: Navigating then comes immediately before
	// this event, and a veto of it has nothing left to stop.
	Error,
	// The page asked for a new window (window.open, target=_blank, a middle
	// click): url, userGesture. The request has already been declined: wx
	// applications handle new windows themselves.
	NewWindow,
	// text is the new title.
	TitleChanged,
	// window.webkit.messageHandlers.<name>.postMessage(value), or
	// window.<name>.postMessage(value): name, text (a posted string as it
	// is; anything else as JSON text), url (the frame's), mainFrame.
	ScriptMessage,
	// The answer to RunScript(source, ScriptResultMode::Event): id, ok,
	// text (the value as wx's RunScript reports it, or the error).
	ScriptResult,
	// A request for a URL of a scheme given to RegisterScheme(): id, url,
	// name (the scheme), method, headers, body. Answer exactly once with
	// RespondToSchemeRequest() or FailSchemeRequest().
	SchemeRequest,
	// The engine no longer wants the answer to SchemeRequest id.
	SchemeRequestStopped,
	// The page entered (flag) or left full screen. Already accepted.
	FullscreenChanged,
	// The page's web process is gone: text is the reason ("crash",
	// "memory", "unresponsive", "exited"...). Reload() starts a new one.
	ProcessExited,
	// Answer to GetCookies(): id, ok, cookies.
	Cookies
};

struct Cookie {
	std::string name, value, domain, path;
	bool httpOnly = false, secure = false, session = false;
};

struct Event {
	EventType type = EventType::Loaded;
	uint64 id = 0;
	std::string url;
	std::string text;
	std::string name;
	std::string method;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	std::vector<Cookie> cookies;
	int32 code = 0;
	ErrorClass errorClass = ErrorClass::Other;
	bool ok = true;
	bool mainFrame = true;
	bool userGesture = false;
	bool flag = false;
	bool blocked = false;
};

// Called on the backend's listener thread. Must be thread-safe and quick.
using EventSink = std::function<void(Event&&)>;

// The last state the engine reported; read with GetState() from any thread.
struct State {
	std::string url;
	std::string title;
	bool loading = false;
	bool canGoBack = false;
	bool canGoForward = false;
	double progress = 0;
	double pageZoom = 1;
	bool softwareRendering = false;
	int32 webProcess = -1;     // team of the page's web process, when known
	bool processGone = false;
};

struct Options {
	// Where cookies, storage and caches are kept: an absolute directory;
	// "" for the application's default WebKit profile. Ignored when
	// privateSession is set (nothing is written to disk then).
	std::string profilePath;
	bool privateSession = false;
	// Sent instead of the engine's own user agent when not empty.
	std::string userAgent;
	// B_FOLLOW_* of the engine's view inside its parent.
	uint32 resizingMode = 0;
	// Create the view hidden (wx shows it when the window is shown).
	bool hidden = false;
};

enum class ScriptResultMode {
	Discard,   // fire and forget (WebView::RunScript in BambuStudio)
	Event,     // EventType::ScriptResult is sent to the sink
	Hold       // kept for WaitScriptResult()
};

struct ScriptResult {
	bool ok = false;
	std::string value;   // or the error text
};

class SummitWebBackend {
public:
	explicit SummitWebBackend(EventSink sink);
	~SummitWebBackend();

	SummitWebBackend(const SummitWebBackend&) = delete;
	SummitWebBackend& operator=(const SummitWebBackend&) = delete;

	// --- process-wide -------------------------------------------------
	// Initialises the engine on the BApplication thread (posting to be_app
	// and waiting when called from another thread). Create() does it too;
	// call it early to pay the cost at a chosen time. B_OK or an error
	// (B_NO_INIT: no be_app; B_TIMED_OUT: be_app is not running).
	static status_t InitializeEngine();
	static bool EngineAvailable();
	// "WebKit 627.1.4 (summit_webkit 1.10.0+..., <revision>)", and WebKit's
	// version alone ("627.1.4"). Neither needs the engine initialised.
	static std::string EngineVersion();
	static std::string EngineVersionNumber();
	// Runs fn on the BApplication thread and waits for it (at most timeout).
	static status_t RunOnApplicationThread(const std::function<void()>& fn,
		bigtime_t timeout = 15000000);

	// --- life cycle ---------------------------------------------------
	// Creates the engine's view as a child of parent (which may or may not
	// be attached to a window yet).
	status_t Create(BView* parent, BRect frame, const Options& options = Options());
	// Removes and deletes the view. After it returns the sink is not called
	// any more. The destructor does the same.
	void Destroy();
	bool IsCreated() const;
	// The engine's BView (a BWebKitView), for GetNativeBackend().
	BView* View() const;

	void SetFrame(BRect frame);
	void Show(bool show);
	void SetFocus();

	// --- navigation ---------------------------------------------------
	void LoadURL(const std::string& url);
	// Shows html as the document of baseURL: relative addresses resolve
	// against baseURL, which is not requested itself.
	void LoadHTML(const std::string& html, const std::string& baseURL);
	void Reload(bool bypassCache = false);
	void Stop();
	void GoBack();
	void GoForward();
	// Answers a Navigating event (flag true) whose handler vetoed it: stops
	// load `generation` if it is still the current one and not finished.
	// No Error event is sent for a load stopped this way.
	void Veto(uint64 generation);

	State GetState() const;

	// --- settings -----------------------------------------------------
	void SetUserAgent(const std::string& userAgent);   // "" restores the engine's
	std::string UserAgent() const;                     // the custom one, or ""
	void SetZoomFactor(double factor);
	void EnableContextMenu(bool enable);
	bool IsContextMenuEnabled() const;

	// --- scripts ------------------------------------------------------
	void AddUserScript(const std::string& source, bool atDocumentStart = true,
		bool allFrames = true);
	// Removes the scripts added with AddUserScript(), not the handler aliases.
	void RemoveAllUserScripts();
	// Offers window.webkit.messageHandlers.<name> and window.<name> (what
	// BambuStudio's pages call: window.wx.postMessage) to the pages; their
	// messages arrive as EventType::ScriptMessage.
	bool AddScriptMessageHandler(const std::string& name);
	bool RemoveScriptMessageHandler(const std::string& name);

	// Runs source in the main frame; its completion value is the result.
	// Returns the request's id (never 0), or 0 when there is no view.
	uint64 RunScript(const std::string& source,
		ScriptResultMode mode = ScriptResultMode::Discard);
	// For ScriptResultMode::Hold: waits at most timeout for the result of
	// request id and takes it. False when it has not arrived (yet).
	bool WaitScriptResult(uint64 id, ScriptResult& result, bigtime_t timeout);
	// The result of a held request is no longer wanted.
	void AbandonScript(uint64 id);
	// RunScript + wait. While waiting, pump (if given) is called about every
	// `slice` so the caller's event loop stays alive; the result does not
	// depend on that loop, so this cannot deadlock on it. False on error or
	// when no result came within timeout; *output then has the reason.
	bool RunScriptSync(const std::string& source, std::string* output,
		bigtime_t timeout = 10000000, const std::function<void()>& pump = nullptr,
		bigtime_t slice = 10000);

	// --- custom URL schemes --------------------------------------------
	// Before the first LoadURL() that needs it. Not for http, https, file...
	bool RegisterScheme(const std::string& scheme);
	void RespondToSchemeRequest(uint64 id, int32 status, const std::string& mimeType,
		const void* data, size_t size,
		const std::vector<std::pair<std::string, std::string>>& headers = {});
	void FailSchemeRequest(uint64 id, const std::string& description);

	// --- editing ------------------------------------------------------
	enum class EditCommand { Cut, Copy, Paste, SelectAll, Undo, Redo };
	void ExecuteEditCommand(EditCommand command);

	// --- cookies ------------------------------------------------------
	// EventType::Cookies with the returned id; url "" for all cookies.
	uint64 GetCookies(const std::string& url);
	// Cookies of url (all when ""), only those named name when not "".
	void DeleteCookies(const std::string& url, const std::string& name);

	struct Impl;

private:
	std::shared_ptr<Impl> fImpl;
};

}  // namespace summitweb

#endif  // SUMMIT_WEB_BACKEND_H
