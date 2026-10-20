#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/MacDarkMode.hpp"

#include <boost/log/trivial.hpp>

#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#if wxUSE_WEBVIEW_EDGE
#include <wx/msw/webview_edge.h>
#elif defined(__WXMAC__)
#include <wx/osx/webview_webkit.h>
#endif
#include <wx/uri.h>
#include <wx/base64.h>
#include <wx/ffile.h>
#if defined(__WIN32__) || defined(__WXMAC__)
#include "wx/private/jsscriptwrapper.h"
#endif

#ifdef __WIN32__
#include <WebView2.h>
#include <Shellapi.h>
#include <slic3r/Utils/Http.hpp>
#elif defined __linux__
#include <gtk/gtk.h>
#define WEBKIT_API
struct WebKitWebView;
struct WebKitJavascriptResult;
struct WebKitSettings;
typedef enum {
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
} WebKitHardwareAccelerationPolicy;
extern "C" {
WEBKIT_API WebKitSettings *webkit_web_view_get_settings(WebKitWebView *web_view);
WEBKIT_API void webkit_settings_set_hardware_acceleration_policy(WebKitSettings *settings, WebKitHardwareAccelerationPolicy policy);
WEBKIT_API guint webkit_get_major_version(void);
WEBKIT_API guint webkit_get_minor_version(void);
WEBKIT_API guint webkit_get_micro_version(void);
WEBKIT_API void
webkit_web_view_run_javascript                       (WebKitWebView             *web_view,
                                                      const gchar               *script,
                                                      GCancellable              *cancellable,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data);
WEBKIT_API WebKitJavascriptResult *
webkit_web_view_run_javascript_finish                (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
						      GError                    **error);
WEBKIT_API void
webkit_javascript_result_unref              (WebKitJavascriptResult *js_result);
}
#endif

#include <cstdlib>
#include <set>
#ifdef __WIN32__
// Run Download and Install in another thread so we don't block the UI thread
DWORD DownloadAndInstallWV2RT() {

  int returnCode = 2; // Download failed
  // Use fwlink to download WebView2 Bootstrapper at runtime and invoke installation
  // Broken/Invalid Https Certificate will fail to download
  // Use of the download link below is governed by the below terms. You may acquire the link
  // for your use at https://developer.microsoft.com/microsoft-edge/webview2/. Microsoft owns
  // all legal right, title, and interest in and to the WebView2 Runtime Bootstrapper
  // ("Software") and related documentation, including any intellectual property in the
  // Software. You must acquire all code, including any code obtained from a Microsoft URL,
  // under a separate license directly from Microsoft, including a Microsoft download site
  // (e.g., https://developer.microsoft.com/microsoft-edge/webview2/).
  // HRESULT hr = URLDownloadToFileW(NULL, L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
  //                               L".\\plugin\\MicrosoftEdgeWebview2Setup.exe", 0, 0);
  fs::path target_file_path = (fs::path(Slic3r::resources_dir()).parent_path() / "MicrosoftEdgeWebView2RuntimeInstallerX64.exe");
  bool downloaded = true;
  if (downloaded) {
    // Either Package the WebView2 Bootstrapper with your app or download it using fwlink
    // Then invoke install at Runtime.
    SHELLEXECUTEINFOW shExInfo = {0};
    shExInfo.cbSize = sizeof(shExInfo);
    shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shExInfo.hwnd = 0;
    shExInfo.lpVerb = L"runas";
    shExInfo.lpFile = target_file_path.generic_wstring().c_str();
    shExInfo.lpParameters = L" /install";
    shExInfo.lpDirectory = 0;
    shExInfo.nShow = 0;
    shExInfo.hInstApp = 0;

    if (ShellExecuteExW(&shExInfo)) {
      WaitForSingleObject(shExInfo.hProcess, INFINITE);
      returnCode = 0; // Install successfull
    } else {
      returnCode = 1; // Install failed
    }
  }
  return returnCode;
}

class WebViewEdge : public wxWebViewEdge
{
public:
    WebViewEdge(const wxWebViewConfiguration& config, bool defer_navigation)
        : wxWebViewEdge(config)
        , m_defer_navigation(defer_navigation)
    {
        Bind(wxEVT_WEBVIEW_CREATED, &WebViewEdge::OnWebViewCreated, this);
    }

    ~WebViewEdge() override
    {
        pendingUrl.clear();
        Unbind(wxEVT_WEBVIEW_CREATED, &WebViewEdge::OnWebViewCreated, this);
    }

    void LoadURL(const wxString& url) override
    {
        if (WebView::IsShuttingDown()) {
            pendingUrl.clear();
            return;
        }

        if (m_defer_navigation && GetNativeBackend() == nullptr) {
            // WebView2 initialization is asynchronous. During GUI recreation,
            // the old controls may still be shutting down while new controls
            // are created. Navigating before wxEVT_WEBVIEW_CREATED can fail
            // with ERROR_INVALID_STATE.
            pendingUrl = url;
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__ << ": WebView2 backend is not ready; deferring URL: "
                << url.ToStdString();
            return;
        }

        pendingUrl.clear();
        wxWebViewEdge::LoadURL(url);
    }

    bool SetUserAgent(const wxString &userAgent)
    {
        bool dark = userAgent.Contains("dark");
        SetColorScheme(dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);

        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            ICoreWebView2Settings *settings;
            HRESULT                hr = webView2->get_Settings(&settings);
            if (hr == S_OK) {
                ICoreWebView2Settings2 *settings2;
                hr = settings->QueryInterface(&settings2);
                if (hr == S_OK) {
                    settings2->put_UserAgent(userAgent.wc_str());
                    settings2->Release();
                    return true;
                }
            }
            settings->Release();
            return false;
        }
        pendingUserAgent = userAgent;
        return true;
    }

    bool SetColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME colorScheme)
    {
        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            ICoreWebView2_13 * webView2_13;
            HRESULT           hr = webView2->QueryInterface(&webView2_13);
            if (hr == S_OK) {
                ICoreWebView2Profile *profile;
                hr = webView2_13->get_Profile(&profile);
                if (hr == S_OK) {
                    profile->put_PreferredColorScheme(colorScheme);
                    profile->Release();
                    return true;
                }
                webView2_13->Release();
            }
            return false;
        }
        pendingColorScheme = colorScheme;
        return true;
    }

    void DoGetClientSize(int *x, int *y) const override
    {
        if (!pendingUserAgent.empty()) {
            auto thiz = const_cast<WebViewEdge *>(this);
            auto userAgent = std::move(thiz->pendingUserAgent);
            thiz->pendingUserAgent.clear();
            thiz->SetUserAgent(userAgent);
        }
        if (pendingColorScheme) {
            auto thiz      = const_cast<WebViewEdge *>(this);
            auto colorScheme = pendingColorScheme;
            thiz->pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
            thiz->SetColorScheme(colorScheme);
        }
        wxWebViewEdge::DoGetClientSize(x, y);
    };
private:
    void OnWebViewCreated(wxWebViewEvent& event)
    {
        if (WebView::IsShuttingDown()) {
            // The backend may have become available after the first shutdown
            // scan. Capture its browser PID before this control is destroyed.
            WebView::BeginShutdown();
            pendingUrl.clear();
            event.Skip();
            return;
        }

        void* backend = GetNativeBackend();
        if (backend == nullptr) {
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__ << ": WebView2 created event received without a backend. webView="
                << (void*) this;
            event.Skip();
            return;
        }

        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__ << ": WebView2 backend ready. webView=" << (void*) this
            << ", backend=" << backend;

        if (!pendingUrl.empty()) {
            const wxString url = std::move(pendingUrl);
            pendingUrl.clear();
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__ << ": loading deferred URL: " << url.ToStdString();
            wxWebViewEdge::LoadURL(url);
        }

        event.Skip();
    }

    const bool m_defer_navigation;
    wxString pendingUrl;
    wxString pendingUserAgent;
    COREWEBVIEW2_PREFERRED_COLOR_SCHEME pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
};

#elif defined __WXOSX__

class WebViewWebKit : public wxWebViewWebKit
{
public:
#if wxCHECK_VERSION(3, 3, 0)
    WebViewWebKit() : wxWebViewWebKit(wxWebView::NewConfiguration(wxWebViewBackendWebKit)) {}
#endif

    void LoadURL(const wxString& url) override
    {
        if (!m_wx_bridge_ready) {
            // Registration runs via CallAfter. Keep only the latest navigation
            // until the document-start bridge script has been installed.
            m_pending_url = url;
            m_has_pending_url = true;
            return;
        }
        wxWebViewWebKit::LoadURL(url);
    }

    bool AddScriptMessageHandler(const wxString& name) override
    {
        const bool added = wxWebViewWebKit::AddScriptMessageHandler(name);
        if (added && name == "wx") {
            m_wx_bridge_ready = true;
            if (m_has_pending_url) {
                const wxString url = m_pending_url;
                m_pending_url.clear();
                m_has_pending_url = false;
                wxWebViewWebKit::LoadURL(url);
            }
        }
        return added;
    }

    ~WebViewWebKit() override
    {
        RemoveScriptMessageHandler("wx");
    }

private:
    bool m_wx_bridge_ready{false};
    bool m_has_pending_url{false};
    wxString m_pending_url;
};

#elif defined __linux__

    // TODO: release webview script message handle in Destructor
    // class WebViewWebGTK : public wxWebview
    // {
    //     ~WebViewWebGTK() override
    //     {
    //         RemoveScriptMessageHandler("wx");
    //     }
    // };

#endif

class FakeWebView : public wxWebView
{
public:
    bool Create(wxWindow* parent, wxWindowID id, const wxString& url, const wxPoint& pos,
                const wxSize& size, long style, const wxString& name) override
    {
        return wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name);
    }
    virtual wxString GetCurrentTitle() const override { return wxString(); }
    virtual wxString GetCurrentURL() const override { return wxString(); }
    virtual bool IsBusy() const override { return false; }
    virtual bool IsEditable() const override { return false; }
    virtual void LoadURL(const wxString& url) override { }
    virtual void Print() override { }
    virtual void RegisterHandler(wxSharedPtr<wxWebViewHandler> handler) override { }
    virtual void Reload(wxWebViewReloadFlags flags = wxWEBVIEW_RELOAD_DEFAULT) override { }
    virtual bool RunScript(const wxString& javascript, wxString* output = NULL) const override { return false; }
    virtual void SetEditable(bool enable = true) override { }
    virtual void Stop() override { }
    virtual bool CanGoBack() const override { return false; }
    virtual bool CanGoForward() const override { return false; }
    virtual void GoBack() override { }
    virtual void GoForward() override { }
    virtual void ClearHistory() override { }
    virtual void EnableHistory(bool enable = true) override { }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetBackwardHistory() override { return {}; }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetForwardHistory() override { return {}; }
    virtual void LoadHistoryItem(wxSharedPtr<wxWebViewHistoryItem> item) override { }
    virtual bool CanSetZoomType(wxWebViewZoomType type) const override { return false; }
    virtual float GetZoomFactor() const override { return 0.0f; }
    virtual wxWebViewZoomType GetZoomType() const override { return wxWebViewZoomType(); }
    virtual void SetZoomFactor(float zoom) override { }
    virtual void SetZoomType(wxWebViewZoomType zoomType) override { }
    virtual bool CanUndo() const override { return false; }
    virtual bool CanRedo() const override { return false; }
    virtual void Undo() override { }
    virtual void Redo() override { }
    virtual void* GetNativeBackend() const override { return nullptr; }
    virtual void DoSetPage(const wxString& html, const wxString& baseUrl) override { }
};

wxDEFINE_EVENT(EVT_WEBVIEW_RECREATED, wxCommandEvent);

static std::vector<wxWebView*> g_webviews;
static std::vector<wxWebView*> g_delay_webviews;
static bool g_webview_shutting_down = false;
#ifdef __WIN32__
static std::set<DWORD> g_webview_browser_pids;
static bool g_webview_atexit_registered = false;
static wxString g_webview_userdata_dir;
static wxString g_webview_userdata_marker;
static bool g_webview_userdata_ephemeral = false;
static std::unique_ptr<wxWebViewConfiguration> g_webview_configuration;
static bool g_force_single_process = false;
#endif
#if defined __linux__
static bool g_linux_webview_schemes_registered = false;
#endif

class WebViewRef : public wxObjectRefData
{
public:
    WebViewRef(wxWebView *webView) : m_webView(webView) {}
    ~WebViewRef() {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " wxWebView address: " << (void*) m_webView;
        auto iter = std::find(g_webviews.begin(), g_webviews.end(), m_webView);
        if (iter != g_webviews.end())
            g_webviews.erase(iter);
        g_delay_webviews.erase(std::remove(g_delay_webviews.begin(), g_delay_webviews.end(), m_webView),
                               g_delay_webviews.end());
    }
    wxWebView *m_webView;
};

#ifdef __WIN32__
static bool is_valid_webview_profile_name(const wxString& name)
{
    return name.StartsWith("profile") && name != "." && name != ".." &&
           !name.Contains("/") && !name.Contains("\\") && !name.Contains(":");
}

static bool claim_webview_profile(const wxString& profile_dir, wxString& marker_path)
{
    if (!wxFileName::Mkdir(profile_dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
        !wxFileName::DirExists(profile_dir)) {
        return false;
    }

    wxFileName marker(profile_dir, ".running");
    HANDLE marker_handle = CreateFileW(marker.GetFullPath().wc_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker_handle == INVALID_HANDLE_VALUE)
        return false;

    const wxString pid = wxString::Format("%lu", static_cast<unsigned long>(wxGetProcessId()));
    const wxScopedCharBuffer pid_utf8 = pid.utf8_str();
    DWORD written = 0;
    WriteFile(marker_handle, pid_utf8.data(), static_cast<DWORD>(pid_utf8.length()), &written, nullptr);
    CloseHandle(marker_handle);
    marker_path = marker.GetFullPath();
    return true;
}

static void save_active_webview_profile(const wxString& active_file_path, const wxString& profile_name)
{
    wxTextFile active_file;
    if (wxFileName::FileExists(active_file_path)) {
        if (!active_file.Open(active_file_path))
            return;
        active_file.Clear();
    } else if (!active_file.Create(active_file_path)) {
        return;
    }

    active_file.AddLine(profile_name);
    active_file.Write();
    active_file.Close();
}

static void initialize_webview_userdata_dir()
{
    wxFileName root(wxStandardPaths::Get().GetTempDir(), "");
    root.AppendDir("slicer_webview_userdata");

    const wxString root_path = root.GetFullPath();
    if (!wxFileName::Mkdir(root_path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
        !wxFileName::DirExists(root_path)) {
        wxFileName fallback(wxStandardPaths::Get().GetTempDir(), "");
        fallback.AppendDir(wxString::Format("slicer_webview_userdata_%lu",
                                           static_cast<unsigned long>(wxGetProcessId())));
        wxFileName::Mkdir(fallback.GetFullPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        g_webview_userdata_dir = fallback.GetFullPath();
        g_webview_userdata_ephemeral = true;
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << ": failed to create reusable profile root; using per-run folder: "
                                   << g_webview_userdata_dir.ToStdString();
        wxSetEnv("WEBVIEW2_USER_DATA_FOLDER", g_webview_userdata_dir);
        return;
    }

    const wxString active_file_path = wxFileName(root_path, "active_profile.txt").GetFullPath();
    wxString profile_name = "profile";
    if (wxFileName::FileExists(active_file_path)) {
        wxTextFile active_file;
        if (active_file.Open(active_file_path) && active_file.GetLineCount() > 0 &&
            is_valid_webview_profile_name(active_file.GetLine(0))) {
            profile_name = active_file.GetLine(0);
        }
        if (active_file.IsOpened())
            active_file.Close();
    }

    wxFileName profile(root_path, "");
    profile.AppendDir(profile_name);
    const bool reused_profile = claim_webview_profile(profile.GetFullPath(), g_webview_userdata_marker);
    if (!reused_profile) {
        profile_name = wxString::Format("profile_%lu_%llu",
                                        static_cast<unsigned long>(wxGetProcessId()),
                                        static_cast<unsigned long long>(GetTickCount64()));
        profile.AssignDir(root_path);
        profile.AppendDir(profile_name);
        if (!claim_webview_profile(profile.GetFullPath(), g_webview_userdata_marker)) {
            g_webview_userdata_ephemeral = true;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": failed to claim reusable and fallback profiles; folder will be removed on exit.";
        }
    }

    g_webview_userdata_dir = profile.GetFullPath();
    save_active_webview_profile(active_file_path, profile_name);
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << (reused_profile ? ": using reusable WebView2 profile: "
                                                  : ": active profile is occupied; using fallback profile: ")
                               << g_webview_userdata_dir.ToStdString();
    wxSetEnv("WEBVIEW2_USER_DATA_FOLDER", g_webview_userdata_dir);
}
#endif

// Roost Print: Rapid Roost's fonts (Manrope for text, Orbitron for headings) on the app's own
// pages. The web views run in their own process and can't see fonts the app registered, and
// pages loaded from disk can't load font files from disk either, so the fonts go in as data.
// Roost Print: the rapidroost.net look on the app's web pages (homepage, device manager, send to
// printer, setup guide). Same tokens as the website (rr.css / dark.css): blue graph paper, black
// outlines, sticker buttons with a hard shadow, yolk for "selected", blue for "on".
// The pages say light/dark in different ways (body.dark, body.dark-theme, body.white-theme,
// body.light-theme, or only the user agent), so roost_web_look_js() boils that down to
// <html data-roost-theme="light|dark"> and names the page folder in data-roost-page; the CSS keys
// off those. Kept in several literals: MSVC can't take one string literal over 16 KB.
static wxString roost_web_look_css()
{
    wxString css;
    // Tokens, element-plus variables, page backgrounds.
    css << R"ROOSTCSS(
html[data-roost-theme]{--rr-paper:#f6f9fb;--rr-surface:#fff;--rr-ink:#000;--rr-text:#1f2a36;--rr-muted:#2b3846;--rr-line:#d5dbe1;--rr-blue:#1e90d8;--rr-link:#1268a8;--rr-fill:#1268a8;--rr-on-fill:#fff;--rr-deep:#0b4f82;--rr-wash:#eef6fc;--rr-yolk:#ffc93c;--rr-on-yolk:#000;--rr-pencil:#c8322a;--rr-chip:#eef1f4;--rr-field:#b9c2cb;--rr-dim:#5b6b7b;--rr-g1:rgba(30,144,216,.16);--rr-g2:rgba(30,144,216,.07);background-color:var(--rr-paper) !important;}
html[data-roost-theme=dark]{color-scheme:dark;--rr-paper:#0e1a27;--rr-surface:#172839;--rr-ink:#dfe8f1;--rr-text:#c9d5e1;--rr-muted:#a8b7c6;--rr-line:#2b3e52;--rr-link:#6cb8ef;--rr-fill:#1268a8;--rr-deep:#a6d4f6;--rr-wash:#13314a;--rr-on-yolk:#10171f;--rr-pencil:#ff7f70;--rr-chip:#22364b;--rr-field:#4a6076;--rr-dim:#9fb0c1;--rr-g1:rgba(30,144,216,.15);--rr-g2:rgba(30,144,216,.06);}
:root[data-roost-theme]{--el-color-primary:#1268a8;--el-color-primary-light-3:#1e90d8;--el-color-primary-light-5:#6cb8ef;--el-color-primary-light-7:#b3d9f3;--el-color-primary-light-8:#d3e8f7;--el-color-primary-light-9:#eef6fc;--el-color-primary-dark-2:#0b4f82;--el-border-radius-base:10px;--el-border-radius-small:8px;--el-border-radius-round:999px;--el-font-family:'Manrope',system-ui,sans-serif;--el-font-weight-primary:700;--el-bg-color:var(--rr-surface);--el-bg-color-overlay:var(--rr-surface);--el-bg-color-page:var(--rr-paper);--el-fill-color-blank:var(--rr-surface);--el-border-color:var(--rr-field);--el-border-color-light:var(--rr-line);--el-border-color-lighter:var(--rr-line);--el-text-color-primary:var(--rr-text);--el-color-danger:#c8322a;}
:root[data-roost-theme=dark]{--el-color-primary-light-3:#1e7fc0;--el-color-primary-light-5:#1a5f92;--el-color-primary-light-7:#16446a;--el-color-primary-light-8:#153a5a;--el-color-primary-light-9:#13314a;--el-color-primary-dark-2:#6cb8ef;--el-text-color-primary:#dfe8f1;--el-text-color-regular:#c9d5e1;--el-text-color-secondary:#9fb0c1;--el-fill-color:#22364b;--el-fill-color-light:#1c2f42;--el-fill-color-lighter:#172839;--el-color-danger:#ff7f70;}
html[data-roost-theme] body{background-color:var(--rr-paper) !important;background-image:linear-gradient(var(--rr-g1) 1px,transparent 1px),linear-gradient(90deg,var(--rr-g1) 1px,transparent 1px),linear-gradient(var(--rr-g2) 1px,transparent 1px),linear-gradient(90deg,var(--rr-g2) 1px,transparent 1px) !important;background-size:120px 120px,120px 120px,24px 24px,24px 24px !important;}
html[data-roost-page=sendToPrinterPage] body,html[data-roost-page=login] body{background-image:none !important;}
[data-roost-theme] a:not([class]){color:var(--rr-link);}
[data-roost-theme] :not(input):not(textarea):not(select):focus-visible{outline:3px solid var(--rr-yolk) !important;outline-offset:3px;}
[data-roost-theme] input[type=checkbox],[data-roost-theme] input[type=radio]{accent-color:var(--rr-fill);}
[data-roost-page=homepage] .ant-layout,[data-roost-page=homepage] .ant-layout-content,[data-roost-page=homepage] .layout-content,[data-roost-page=homepage] .empty-wrap,[data-roost-page=homepage] .HomePage{background:transparent !important;}
[data-roost-page=homepage] .ant-layout-sider{background:var(--rr-surface) !important;border-right:2px solid var(--rr-ink);}
[data-roost-page=homepage] .container-Account{border:2px solid var(--rr-ink);border-radius:14px;background:var(--rr-paper) !important;}
[data-roost-page=homepage] .shop-link-fixed{border:2px solid var(--rr-ink) !important;border-radius:999px !important;box-shadow:3px 3px 0 var(--rr-ink) !important;background:var(--rr-surface) !important;}
[data-roost-page=homepage] .shop-link-fixed *{color:var(--rr-ink) !important;font-weight:800;}
[data-roost-page=homepage] .Header .line{background:var(--rr-ink) !important;height:2px !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-new-project,[data-roost-page=homepage] .HomePage .container-btns .btn-open-project{border:2px solid var(--rr-ink) !important;border-radius:16px !important;box-shadow:4px 4px 0 var(--rr-ink);opacity:1 !important;transition:transform .1s ease,box-shadow .1s ease !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-new-project{background:var(--rr-fill) !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-open-project{background:var(--rr-surface) !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-new-project:hover,[data-roost-page=homepage] .HomePage .container-btns .btn-open-project:hover{transform:translate(-2px,-2px);box-shadow:6px 6px 0 var(--rr-ink);}
[data-roost-page=homepage] .HomePage .container-btns .btn-new-project:active,[data-roost-page=homepage] .HomePage .container-btns .btn-open-project:active{transform:translate(2px,2px);box-shadow:2px 2px 0 var(--rr-ink);}
[data-roost-page=homepage] .HomePage .container-btns .btn-new-project .title,[data-roost-page=homepage] .HomePage .container-btns .btn-new-project .subtitle,[data-roost-page=homepage] .HomePage .container-btns .btn-new-project .circle-btn{color:#fff !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-new-project .circle-btn{background:rgba(255,255,255,.18) !important;border:2px solid #fff;}
[data-roost-page=homepage] .HomePage .container-btns .btn-open-project .title,[data-roost-page=homepage] .HomePage .container-btns .btn-open-project .circle-btn{color:var(--rr-ink) !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-open-project .subtitle{color:var(--rr-muted) !important;}
[data-roost-page=homepage] .HomePage .container-btns .btn-open-project .circle-btn{background:var(--rr-yolk) !important;color:var(--rr-on-yolk) !important;border:2px solid var(--rr-ink);}
[data-roost-page=homepage] .HomePage .container-btns .title{font-weight:800 !important;}
[data-roost-page=deviceMgr] #app>div,[data-roost-page=deviceMgr] .main{background:transparent !important;}
[data-roost-page=deviceMgr] .header-wrapper,[data-roost-page=deviceMgr] .device-detail-header-wrapper,[data-roost-page=deviceMgr] .el-tabs--border-card,[data-roost-page=deviceMgr] .el-tabs--border-card>.el-tabs__header,[data-roost-page=deviceMgr] .device-detail-header-wrapper .el-tabs__nav-wrap{background:var(--rr-surface) !important;box-shadow:none !important;border-color:transparent !important;}
[data-roost-page=deviceMgr] .header-wrapper,[data-roost-page=deviceMgr] .device-detail-header-wrapper{border-bottom:2px solid var(--rr-ink) !important;}
[data-roost-page=deviceMgr] .login-tip-area{background:var(--rr-surface) !important;border:2px solid var(--rr-ink) !important;border-radius:14px !important;box-shadow:4px 4px 0 var(--rr-ink);color:var(--rr-text) !important;}
[data-roost-page=deviceMgr] .highlight{color:var(--rr-link) !important;font-weight:800;}
[data-roost-page=sendToPrinterPage] .main,[data-roost-page=sendToPrinterPage] .carousel-container{background:transparent !important;}
[data-roost-page=sendToPrinterPage] .bottom-item:not(.print-options),[data-roost-page=sendToPrinterPage] .el-carousel__item--card{background:var(--rr-surface) !important;border:2px solid var(--rr-ink) !important;border-radius:14px !important;box-shadow:none !important;}
[data-roost-page=sendToPrinterPage] .el-carousel__item--card.is-active{box-shadow:4px 4px 0 var(--rr-ink) !important;}
[data-roost-page=sendToPrinterPage] .cmd-btn{background:var(--rr-surface) !important;border-top:2px solid var(--rr-ink);border-radius:0 !important;}
[data-roost-page=sendToPrinterPage] .rack-line{border-color:var(--rr-line) !important;}
[data-roost-theme] .el-input{border:0 !important;}
)ROOSTCSS";
    // Buttons: stickers. Text, link and icon-only buttons stay flat.
    css << R"ROOSTCSS(
[data-roost-theme] .el-button:not(.is-text):not(.is-link):not(.is-circle),[data-roost-theme] .ant-btn:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle),[data-roost-theme] .NormalBtn,[data-roost-theme] .GrayBtn,[data-roost-theme] .SmallBtn,[data-roost-theme] .SmallBtn_Green,[data-roost-theme] #ErrorBtn,[data-roost-theme] #AddModelInfoBtn,[data-roost-page=guide] .add-printer-setting-area button,[data-roost-page=guide] #selectAll,[data-roost-page=guide] #AcceptBtn,[data-roost-page=guide] #PreBtn,[data-roost-page=guide] .confirm-btn{border:2px solid var(--rr-ink) !important;border-radius:999px !important;box-shadow:3px 3px 0 var(--rr-ink) !important;background:var(--rr-surface) !important;color:var(--rr-ink) !important;font-weight:800 !important;transition:transform .1s ease,box-shadow .1s ease !important;opacity:1;}
[data-roost-theme] .el-button.el-button--primary:not(.is-text):not(.is-link):not(.is-circle),[data-roost-theme] .ant-btn.ant-btn-primary:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle),[data-roost-theme] .NormalBtn.NormalBtn,[data-roost-theme] .SmallBtn_Green.SmallBtn_Green,[data-roost-theme] #ErrorBtn#ErrorBtn,[data-roost-theme] #AddModelInfoBtn#AddModelInfoBtn,[data-roost-page=guide] #selectAll#selectAll,[data-roost-page=guide] #AcceptBtn#AcceptBtn,[data-roost-page=guide] .confirm-btn.confirm-btn,[data-roost-page=sendToPrinterPage] .el-button.el-button.start-print.start-print:not(.is-text){background:var(--rr-fill) !important;color:var(--rr-on-fill) !important;}
[data-roost-theme] .el-button.el-button--danger:not(.is-text):not(.is-link):not(.is-circle),[data-roost-theme] .ant-btn.ant-btn-dangerous:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle){background:var(--rr-pencil) !important;color:#fff !important;}
[data-roost-theme] .el-button:not(.is-text):not(.is-link):not(.is-circle):hover,[data-roost-theme] .ant-btn:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle):hover,[data-roost-theme] .NormalBtn:hover,[data-roost-theme] .GrayBtn:hover,[data-roost-theme] .SmallBtn:hover,[data-roost-theme] .SmallBtn_Green:hover,[data-roost-theme] #ErrorBtn:hover,[data-roost-theme] #AddModelInfoBtn:hover,[data-roost-page=guide] .add-printer-setting-area button:hover,[data-roost-page=guide] #selectAll:hover,[data-roost-page=guide] #AcceptBtn:hover,[data-roost-page=guide] #PreBtn:hover,[data-roost-page=guide] .confirm-btn:hover{transform:translate(-2px,-2px);box-shadow:5px 5px 0 var(--rr-ink) !important;}
[data-roost-theme] .el-button:not(.is-text):not(.is-link):not(.is-circle):active,[data-roost-theme] .ant-btn:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle):active,[data-roost-theme] .NormalBtn:active,[data-roost-theme] .GrayBtn:active,[data-roost-theme] .SmallBtn:active,[data-roost-theme] .SmallBtn_Green:active,[data-roost-theme] #ErrorBtn:active,[data-roost-theme] #AddModelInfoBtn:active{transform:translate(2px,2px);box-shadow:1px 1px 0 var(--rr-ink) !important;}
[data-roost-theme] .el-button.is-disabled:not(.is-text):not(.is-link):not(.is-circle),[data-roost-theme] .el-button.is-disabled:not(.is-text):not(.is-link):not(.is-circle):hover,[data-roost-theme] .ant-btn:disabled:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle),[data-roost-theme] .ant-btn:disabled:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle):hover{opacity:.5;transform:none;box-shadow:3px 3px 0 var(--rr-ink) !important;cursor:not-allowed;}
[data-roost-theme] .el-button--small:not(.is-text):not(.is-link):not(.is-circle),[data-roost-theme] .ant-btn-sm:not(.ant-btn-text):not(.ant-btn-link):not(.ant-btn-icon-only):not(.ant-btn-circle),[data-roost-theme] .SmallBtn,[data-roost-theme] .SmallBtn_Green{box-shadow:2px 2px 0 var(--rr-ink) !important;border-width:1.5px !important;}
[data-roost-theme] .el-button.is-text,[data-roost-theme] .el-button.is-link,[data-roost-theme] .ant-btn-link{color:var(--rr-link);font-weight:700;}
[data-roost-theme] .el-button+.el-button{margin-left:14px;}
)ROOSTCSS";
    // Selected = yolk (tabs, segmented and radio buttons, menus); on = blue (switches, checks).
    css << R"ROOSTCSS(
[data-roost-theme] .el-tabs__item{border-radius:999px !important;border:2px solid transparent !important;font-weight:800 !important;transition:background .15s ease;}
[data-roost-theme] .el-tabs__item:not(.is-active):not(.is-disabled):hover{background:var(--rr-wash) !important;color:var(--rr-ink) !important;}
[data-roost-theme] .el-tabs__item.is-active,[data-roost-theme] .el-tabs--border-card>.el-tabs__header .el-tabs__item.is-active,[data-roost-theme] .el-tabs--card>.el-tabs__header .el-tabs__item.is-active{background:var(--rr-yolk) !important;border:2px solid var(--rr-ink) !important;color:var(--rr-on-yolk) !important;}
[data-roost-theme] .el-tabs__item.is-active *{color:var(--rr-on-yolk) !important;}
[data-roost-theme] .el-tabs__active-bar{display:none !important;}
[data-roost-theme] .el-tabs--card>.el-tabs__header .el-tabs__nav,[data-roost-theme] .el-tabs--card>.el-tabs__header .el-tabs__item{border-color:transparent;}
[data-roost-theme] .el-radio-button.is-active .el-radio-button__inner,[data-roost-theme] .el-radio-button__original-radio:checked+.el-radio-button__inner,[data-roost-theme] .el-segmented__item.is-selected,[data-roost-theme] .el-segmented__item-selected,[data-roost-theme] .ant-radio-button-wrapper-checked:not(.ant-radio-button-wrapper-disabled),[data-roost-theme] .ant-segmented-item-selected{background:var(--rr-yolk) !important;border-color:var(--rr-ink) !important;color:var(--rr-on-yolk) !important;box-shadow:none !important;font-weight:800;}
[data-roost-theme] .el-radio-button__inner,[data-roost-theme] .ant-radio-button-wrapper{font-weight:700;}
[data-roost-theme] .el-segmented,[data-roost-theme] .el-segmented__item,[data-roost-theme] .el-segmented__item-selected,[data-roost-theme] .ant-segmented,[data-roost-theme] .ant-segmented-item,[data-roost-theme] .ant-segmented-thumb{border-radius:999px !important;}
[data-roost-theme] .el-segmented__item-selected{border:2px solid var(--rr-ink);}
[data-roost-theme] .ant-segmented-thumb{background:var(--rr-yolk) !important;}
[data-roost-theme] .ant-menu .ant-menu-item,[data-roost-theme] .ant-menu .ant-menu-submenu-title{border-radius:999px !important;font-weight:700;}
[data-roost-theme] .ant-menu .ant-menu-item:not(.ant-menu-item-selected):hover,[data-roost-theme] .ant-menu .ant-menu-submenu-title:hover{background:var(--rr-wash) !important;}
[data-roost-theme] .ant-menu .ant-menu-item-selected,[data-roost-theme] .ant-menu .ant-menu-item-selected:hover{background:var(--rr-yolk) !important;color:var(--rr-on-yolk) !important;box-shadow:inset 0 0 0 2px var(--rr-ink);font-weight:800;}
[data-roost-theme] .ant-menu .ant-menu-item-selected *{color:var(--rr-on-yolk) !important;}
[data-roost-theme] .ant-tabs .ant-tabs-tab{font-weight:700;}
[data-roost-theme] .ant-tabs .ant-tabs-tab.ant-tabs-tab-active .ant-tabs-tab-btn{color:var(--rr-ink) !important;font-weight:800;}
[data-roost-theme] .ant-tabs .ant-tabs-ink-bar{background:var(--rr-yolk) !important;height:4px !important;border-radius:2px;}
[data-roost-theme] .el-dropdown-menu__item:not(.is-disabled):hover,[data-roost-theme] .el-dropdown-menu__item:not(.is-disabled):focus,[data-roost-theme] .el-select-dropdown__item.is-hovering,[data-roost-theme] .el-select-dropdown__item.hover,[data-roost-theme] .el-select-dropdown__item:hover,[data-roost-theme] .ant-select-item-option-active,[data-roost-theme] .ant-dropdown-menu-item:hover{background:var(--rr-wash) !important;}
[data-roost-theme] .el-select-dropdown__item.is-selected,[data-roost-theme] .el-select-dropdown__item.selected,[data-roost-theme] .ant-select-item-option-selected{color:var(--rr-link) !important;font-weight:800 !important;}
[data-roost-theme] .el-switch{--el-switch-on-color:var(--rr-fill);--el-switch-off-color:var(--rr-field);}
[data-roost-theme] .el-switch.is-checked .el-switch__core{background-color:var(--rr-fill) !important;border-color:var(--rr-fill) !important;}
[data-roost-theme] .ant-switch.ant-switch-checked,[data-roost-theme] .ant-switch.ant-switch-checked:hover{background:var(--rr-fill) !important;}
[data-roost-theme] .el-checkbox__input.is-checked .el-checkbox__inner,[data-roost-theme] .el-checkbox__input.is-indeterminate .el-checkbox__inner,[data-roost-theme] .el-radio__input.is-checked .el-radio__inner,[data-roost-theme] .ant-checkbox-checked .ant-checkbox-inner,[data-roost-theme] .ant-checkbox-indeterminate .ant-checkbox-inner,[data-roost-theme] .ant-radio-checked .ant-radio-inner{background-color:var(--rr-fill) !important;border-color:var(--rr-fill) !important;}
[data-roost-theme] .ant-checkbox-indeterminate .ant-checkbox-inner:after{background:#fff !important;}
[data-roost-theme] .el-checkbox__input.is-checked+.el-checkbox__label,[data-roost-theme] .el-radio__input.is-checked+.el-radio__label{color:var(--rr-link) !important;}
[data-roost-theme] .el-checkbox__inner,[data-roost-theme] .ant-checkbox-inner{border-radius:4px !important;}
[data-roost-theme] .el-slider{--el-slider-main-bg-color:var(--rr-fill);}
[data-roost-theme] .el-progress-bar__inner,[data-roost-theme] .ant-progress-bg{background-color:var(--rr-blue);}
[data-roost-page=guide] .is-selected,[data-roost-page=guide] .toggle-button-isChecked{background:var(--rr-yolk) !important;color:var(--rr-on-yolk) !important;box-shadow:inset 0 0 0 2px var(--rr-ink);font-weight:800;}
[data-roost-page=guide] .toggle-button,[data-roost-page=guide] .toggle-button-white,[data-roost-page=guide] .button-wrapper,[data-roost-page=guide] .button-wrapper button{border-radius:999px !important;}
[data-roost-page=guide] .toggle-button:not(.toggle-button-isChecked):hover,[data-roost-page=guide] .tree-node-item:not(.is-selected):hover,[data-roost-page=guide] .tree-node-item-white:not(.is-selected):hover{background:var(--rr-wash) !important;color:var(--rr-ink) !important;}
)ROOSTCSS";
    // Cards, dialogs, popups, inputs, chips.
    css << R"ROOSTCSS(
[data-roost-theme] .el-dialog,[data-roost-theme] .el-message-box,[data-roost-theme] .ant-modal .ant-modal-content{border:2px solid var(--rr-ink) !important;border-radius:14px !important;box-shadow:6px 6px 0 var(--rr-ink) !important;}
[data-roost-theme] .el-dialog__title,[data-roost-theme] .el-message-box__title,[data-roost-theme] .ant-modal-title{font-weight:800 !important;}
[data-roost-theme] .el-card,[data-roost-theme] .ant-card{border:2px solid var(--rr-ink) !important;border-radius:14px !important;}
[data-roost-theme] .el-card.is-hover-shadow:hover,[data-roost-theme] .ant-card-hoverable:hover{box-shadow:4px 4px 0 var(--rr-ink) !important;}
[data-roost-theme] .el-popper.is-light,[data-roost-theme] .el-dropdown__popper.el-popper,[data-roost-theme] .el-popover.el-popper,[data-roost-theme] .ant-dropdown .ant-dropdown-menu,[data-roost-theme] .ant-select-dropdown,[data-roost-theme] .ant-popover .ant-popover-inner{border:2px solid var(--rr-ink) !important;border-radius:10px !important;box-shadow:4px 4px 0 var(--rr-ink) !important;}
[data-roost-theme] .el-popper.is-light .el-popper__arrow::before{border-color:var(--rr-ink) !important;}
[data-roost-theme] .el-message,[data-roost-theme] .ant-message .ant-message-notice-content,[data-roost-theme] .el-notification,[data-roost-theme] .ant-notification-notice{border:2px solid var(--rr-ink) !important;border-radius:12px !important;box-shadow:4px 4px 0 var(--rr-ink) !important;}
[data-roost-theme] .el-input__wrapper,[data-roost-theme] .el-select__wrapper,[data-roost-theme] .el-textarea__inner{border-radius:10px !important;box-shadow:0 0 0 1.5px var(--rr-field) inset !important;}
[data-roost-theme] .el-input__wrapper:hover,[data-roost-theme] .el-select__wrapper:hover,[data-roost-theme] .el-textarea__inner:hover{box-shadow:0 0 0 1.5px var(--rr-dim) inset !important;}
[data-roost-theme] .el-input__wrapper.is-focus,[data-roost-theme] .el-select__wrapper.is-focused,[data-roost-theme] .el-select .is-focus .el-input__wrapper,[data-roost-theme] .el-textarea__inner:focus{box-shadow:0 0 0 2px var(--rr-blue) inset !important;}
[data-roost-theme] .is-error .el-input__wrapper,[data-roost-theme] .is-error .el-select__wrapper,[data-roost-theme] .is-error .el-textarea__inner{box-shadow:0 0 0 2px var(--rr-pencil) inset !important;}
[data-roost-theme] .el-input-group__append,[data-roost-theme] .el-input-group__prepend{border-radius:10px;}
[data-roost-theme] .el-input-group--append .el-input__wrapper{border-top-right-radius:0 !important;border-bottom-right-radius:0 !important;}
[data-roost-theme] .el-input-group--prepend .el-input__wrapper{border-top-left-radius:0 !important;border-bottom-left-radius:0 !important;}
[data-roost-theme] .el-input-number{border-radius:10px;}
[data-roost-theme] .ant-input,[data-roost-theme] .ant-input-affix-wrapper,[data-roost-theme] .ant-input-number,[data-roost-theme] .ant-picker,[data-roost-theme] .ant-select:not(.ant-select-customize-input) .ant-select-selector{border-radius:10px !important;border-width:1.5px !important;border-color:var(--rr-field) !important;box-shadow:none !important;}
[data-roost-theme] .ant-input-affix-wrapper .ant-input{border:0 !important;border-radius:0 !important;}
[data-roost-theme] .ant-input:focus,[data-roost-theme] .ant-input-focused,[data-roost-theme] .ant-input-affix-wrapper-focused,[data-roost-theme] .ant-input-number-focused,[data-roost-theme] .ant-picker-focused,[data-roost-theme] .ant-select-focused .ant-select-selector{border-color:var(--rr-blue) !important;border-width:2px !important;}
[data-roost-page=guide] input[type=text],[data-roost-page=guide] input[type=search],[data-roost-page=guide] input:not([type]),[data-roost-page=guide] select,[data-roost-page=guide] textarea,[data-roost-page=model] input[type=text],[data-roost-page=model] textarea{border:2px solid var(--rr-field) !important;border-radius:10px !important;background:var(--rr-surface) !important;color:var(--rr-text) !important;outline:none !important;box-shadow:none !important;}
[data-roost-page=guide] input[type=text]:focus,[data-roost-page=guide] input[type=search]:focus,[data-roost-page=guide] input:not([type]):focus,[data-roost-page=guide] select:focus,[data-roost-page=guide] textarea:focus,[data-roost-page=model] input[type=text]:focus,[data-roost-page=model] textarea:focus{border-color:var(--rr-blue) !important;}
[data-roost-page=guide] .search{border-radius:10px;}
[data-roost-page=guide] .card-list-item,[data-roost-page=guide] .card-area,[data-roost-page=guide] .search-printer-empty,[data-roost-page=guide] .TextArea1,[data-roost-page=guide] .card-list-item-checked-printer-wrapper{border:2px solid var(--rr-ink) !important;border-radius:14px !important;background:var(--rr-surface) !important;}
[data-roost-page=guide] .search-printer-empty{border-style:dashed !important;}
[data-roost-page=guide] .card-list-item:hover{box-shadow:4px 4px 0 var(--rr-ink);}
[data-roost-page=guide] .card-list-add-nozzle button{border:1.5px solid var(--rr-field) !important;border-radius:999px !important;background:var(--rr-chip) !important;color:var(--rr-text) !important;font-weight:700;}
[data-roost-page=guide] .main-container,[data-roost-page=guide] .card-container,[data-roost-page=guide] #userContainer,[data-roost-page=guide] .toggle-user-type,[data-roost-page=guide] .toggle-user-type-white,[data-roost-page=guide] .acceptArea,[data-roost-page=guide] .acceptArea-white{background:transparent !important;}
[data-roost-page=guide] #Title,[data-roost-page=guide] #Title div{color:var(--rr-ink) !important;font-family:'Orbitron','Manrope',sans-serif !important;font-weight:700;}
[data-roost-page=guide] .HyperLink{color:var(--rr-link) !important;}
[data-roost-theme] .el-tag,[data-roost-theme] .ant-tag{border-radius:999px !important;border:1.5px solid var(--rr-field) !important;font-weight:700;}
[data-roost-theme] .el-tag.el-tag--primary,[data-roost-theme] .el-tag:not([class*=el-tag--]){border-color:var(--rr-field) !important;}
)ROOSTCSS";
    return css;
}

// Sets <html data-roost-theme> and <html data-roost-page> (see roost_web_look_css()).
static const char* roost_web_look_js()
{
    return R"ROOSTJS(
var rrPage=(function(){var p=location.pathname||'';var m=p.match(/\/web\/([^\/]+)\//)||p.match(/^\/([^\/]+)\//);return m?m[1]:'';})();
var rrTheme=function(){var dE=document.documentElement;if(!dE)return;var d=null;
var test=function(c){if(!c)return;if(c.contains('dark')||c.contains('dark-theme'))d=1;else if(c.contains('white-theme')||c.contains('light-theme'))d=0;};
if(document.body)test(document.body.classList);if(d===null)test(dE.classList);
if(d===null)d=/\(dark/i.test(navigator.userAgent)?1:0;
var v=d?'dark':'light';if(dE.getAttribute('data-roost-theme')!==v)dE.setAttribute('data-roost-theme',v);};
var rrWatch=function(){var dE=document.documentElement;if(!dE)return false;
if(rrPage)dE.setAttribute('data-roost-page',rrPage);rrTheme();
new MutationObserver(rrTheme).observe(dE,{attributes:true,attributeFilter:['class']});
var onBody=function(){if(!document.body)return;rrTheme();new MutationObserver(rrTheme).observe(document.body,{attributes:true,attributeFilter:['class']});};
if(document.body)onBody();else document.addEventListener('DOMContentLoaded',onBody);return true;};
if(!rrWatch())document.addEventListener('DOMContentLoaded',rrWatch);
)ROOSTJS";
}

static const wxString& roost_web_theme_script()
{
    static wxString script;
    if (!script.empty())
        return script;
    auto font_data = [](const char* file) -> wxString {
        wxFFile f(wxString::FromUTF8(Slic3r::resources_dir() + "/fonts/" + file), "rb");
        if (!f.IsOpened())
            return wxString();
        wxMemoryBuffer buf;
        const wxFileOffset len = f.Length();
        if (len <= 0)
            return wxString();
        f.Read(buf.GetWriteBuf(len), len);
        buf.UngetWriteBuf(len);
        return "data:font/ttf;base64," + wxBase64Encode(buf);
    };
    const wxString regular = font_data("Manrope-Regular.ttf");
    const wxString bold    = font_data("Manrope-Bold.ttf");
    const wxString display = font_data("Orbitron-Bold.ttf");
    if (regular.empty())
        return script = " "; // fonts missing: inject nothing (a space so this isn't rebuilt)
    wxString css;
    css << "@font-face{font-family:'Manrope';font-weight:100 500;src:url(" << regular << ") format('truetype');}";
    if (!bold.empty())
        css << "@font-face{font-family:'Manrope';font-weight:600 900;src:url(" << bold << ") format('truetype');}";
    if (!display.empty())
        css << "@font-face{font-family:'Orbitron';font-weight:100 900;src:url(" << display << ") format('truetype');}";
    // Everything but icon fonts.
    css << "html,body,body *:not(i):not([class*='icon']):not([class*='Icon']):not(code):not(pre)"
           "{font-family:'Manrope',system-ui,'Segoe UI',sans-serif !important;}"
           "body h1:not(#roost-x),body h2:not(#roost-x){font-family:'Orbitron','Manrope',sans-serif !important;letter-spacing:.02em;}"
           "::selection{background:#1e90d8;color:#fff;}";
    css << roost_web_look_css();
    css.Replace("\r", "");
    css.Replace("\\", "\\\\");
    css.Replace("'", "\\'");
    css.Replace("\n", " ");
    // The homepage comes from the app's own little web server (localhost), the rest from disk.
    script = "(function(){try{var h=location.hostname;"
             "if(location.protocol!=='file:'&&h!=='localhost'&&h!=='127.0.0.1')return;"
             "var s=document.createElement('style');s.id='roost-theme';s.textContent=" +
             wxString("'") + css + "';"
             "var add=function(){if(document.getElementById('roost-theme'))return true;"
             "var p=document.head||document.documentElement;if(!p)return false;p.appendChild(s);return true;};"
             "if(!add())document.addEventListener('DOMContentLoaded',add);" +
             wxString(roost_web_look_js()) +
             // No Creality names in what the pages show (text only; values and data are untouched).
             "var R=[[/Creality3DSlicer|CrealityPrint|Creality Print/g,'Roost Print'],[/Creality ?Cloud/g,'Cloud'],"
             "[/Creality filament/g,'Polymaker filament'],[/Creality printers/g,'printers'],[/Creality printer/g,'printer'],"
             "[/Creality /g,''],[/Creality/g,'Rapid Roost']];"
             // Homepage: no links to features Roost Print removed (mall, feedback, online models and params).
             "var HIDE={'Creality Mall':1,'Mall':1,'Feedback':1,'Online Params':1,'Online Models':1};"
             "var gone=function(n){var v=(n.nodeValue||'').trim();if(document.documentElement.getAttribute('data-roost-page')!=='homepage')return false;"
             "if(v.indexOf('Only one step to creation')===0){n.nodeValue='Open a project, or drag one in, to get started.';return true;}"
             "if(v==='and take a look!'){n.nodeValue='';return true;}"
             "if(HIDE[v]&&n.parentNode&&n.parentNode.closest){var e=n.parentNode.closest('li,a,button,[role=menuitem],.el-menu-item')||n.parentNode;e.style.setProperty('display','none','important');return true;}return false;};"
             "var fix=function(n){if(n.nodeType===3){if(gone(n))return;var t=n.nodeValue;if(t&&t.indexOf('Creality')>=0){for(var i=0;i<R.length;i++)t=t.replace(R[i][0],R[i][1]);n.nodeValue=t;}return;}"
             "if(n.nodeType!==1)return;var tg=n.tagName;if(tg==='SCRIPT'||tg==='STYLE'||tg==='TEXTAREA'||tg==='INPUT')return;"
             "if(n.title&&n.title.indexOf('Creality')>=0){var u=n.title;for(var k=0;k<R.length;k++)u=u.replace(R[k][0],R[k][1]);n.title=u;}"
             "for(var c=n.firstChild;c;c=c.nextSibling)fix(c);};"
             "var start=function(){if(!document.body)return;fix(document.body);if(document.title.indexOf('Creality')>=0){var dt=document.title;for(var j=0;j<R.length;j++)dt=dt.replace(R[j][0],R[j][1]);document.title=dt;}"
             "new MutationObserver(function(ms){for(var m=0;m<ms.length;m++){var x=ms[m];if(x.type==='characterData')fix(x.target);else for(var a=0;a<x.addedNodes.length;a++)fix(x.addedNodes[a]);}})"
             ".observe(document.body,{childList:true,subtree:true,characterData:true});};"
             "if(document.body)start();else document.addEventListener('DOMContentLoaded',start);"
             "}catch(e){}})();";
    return script;
}

wxWebView* WebView::CreateWebView(wxWindow * parent, wxString const & url)
{
    if (g_webview_shutting_down) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": using inert view during application shutdown";
        auto* webView = new FakeWebView;
        webView->Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                        wxDefaultSize, wxBORDER_NONE, wxWebViewNameStr);
        return webView;
    }

    bool using_edge_fixed = false;
#if wxUSE_WEBVIEW_EDGE
    // Check if a fixed version of edge is present in
    // $executable_path/edge_fixed and use it
    wxFileName edgeFixedDir(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir.SetFullName("");
    edgeFixedDir.AppendDir("edge_fixed");
    using_edge_fixed = edgeFixedDir.DirExists();
    if (using_edge_fixed) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }
    // parepare for single process mode, but edge_fixed2 is not used now, so we don't set it.
    wxFileName edgeFixedDir2(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir2.SetFullName("");
    edgeFixedDir2.AppendDir("edge_fixed2");
    if (edgeFixedDir2.DirExists()) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir2.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }
    if (g_webview_userdata_dir.empty())
        initialize_webview_userdata_dir();
#endif
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << url2.ToUTF8();

#ifdef __WIN32__
    wxString previous_browser_args;
    const bool had_previous_args = wxGetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", &previous_browser_args);
    const bool use_single_process = g_force_single_process && !using_edge_fixed;
    if (g_force_single_process && using_edge_fixed) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": edge_fixed detected; ignoring WebView2 single-process mode.";
    }
    if (use_single_process) {
        wxSetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", wxS("--single-process"));
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": enabling WebView2 single-process mode for creation.";
    }
    // Keep one Edge configuration alive for the whole process. Reusing its
    // underlying WebView2 environment prevents rapid GUI recreation from
    // closing one environment while the next controls are being initialized.
    if (!g_webview_configuration) {
        wxWebViewConfiguration config = wxWebView::NewConfiguration(wxWebViewBackendEdge);
        config.SetDataPath(g_webview_userdata_dir);
        g_webview_configuration = std::make_unique<wxWebViewConfiguration>(config);
    }
    // Initial startup keeps the old direct-navigation behaviour. GUI recreation
    // snapshots a deferred-navigation policy because m_is_recreating_gui may be
    // reset before the asynchronous WebView2 creation event is delivered.
    const bool defer_navigation = Slic3r::GUI::wxGetApp().is_recreating_gui();
    wxWebView* webView = new WebViewEdge(*g_webview_configuration, defer_navigation);
    BOOST_LOG_TRIVIAL(warning)
        << __FUNCTION__ << ": WebView2 navigation mode="
        << (defer_navigation ? "deferred (GUI recreation)" : "direct (initial startup)");
#elif defined(__WXOSX__)
    wxWebView *webView = new WebViewWebKit;
#else
    auto webView = wxWebView::New();
#endif
    if (webView) {
        webView->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
#ifdef __WIN32__
        webView->SetUserAgent(wxString::Format("Creality-Slicer/v%s (%s) Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/107.0.0.0 Safari/537.36 Edg/107.0.1418.52", SLIC3R_VERSION, 
            Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
        if (defer_navigation) {
            // During GUI recreation, wait for wxEVT_WEBVIEW_CREATED before
            // navigating to avoid ERROR_INVALID_STATE while old controls close.
            webView->Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
            if (!url2.empty())
                webView->LoadURL(url2);
        } else {
            // On initial startup let wxWebView2 handle the initial URL as part
            // of Create(), matching the pre-10ece62d62 behaviour.
            webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        }
        // We register the wxfs:// protocol for testing purposes
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#elif defined __linux__
        if (webView) {
            std::ifstream osRelease("/etc/os-release");
            std::string   line;
            bool          isUOS = false;
            while (std::getline(osRelease, line)) {
                if (line.find("UOS") != std::string::npos || line.find("UnionTech") != std::string::npos) {
                    isUOS = true;
                    break;
                }
            }
            if (isUOS) {
                webView->SetUserAgent(
                    wxString::Format("Creality-Slicer/v%s (%s; UOS)", SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
                setenv("WEBKIT_DISABLE_TLS_VERIFICATION", "0", 0);
                setenv("G_TLS_GNUTLS_PRIORITY", "NORMAL:-VERS-ALL:+VERS-TLS1.2", 1);
                setenv("WEBKIT_TLS_ERRORS_POLICY", "ignore-tls-errors", 0);
                BOOST_LOG_TRIVIAL(info) << "Set TLS 1.2 as default for WebView on Linux";
            } else {
                webView->SetUserAgent(
                    wxString::Format("Creality-Slicer/v%s (%s; Linux) Mozilla/5.0 (X11; Linux) AppleWebKit/537.36 (KHTML, like Gecko)",
                                     SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
            }
            if (!g_linux_webview_schemes_registered) {
                webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
                webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
                g_linux_webview_schemes_registered = true;
            }
            webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        }
#else
        // With WKWebView handlers need to be registered before creation
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
        // Create only the empty document; business pages must wait for "wx".
        webView->Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        webView->SetUserAgent(wxString::Format("Creality-Slicer/v%s (%s) Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)", SLIC3R_VERSION,
                                               Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
        if (!url2.empty())
            webView->LoadURL(url2);
#endif
#ifdef __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_setTransparentBackground(wkWebView);
#endif

        auto addScriptMessageHandler = [] (wxWebView *webView) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": begin to add script message handler for wx.";
            Slic3r::GUI::wxGetApp().set_adding_script_handler(true);
            if (!webView->AddScriptMessageHandler("wx"))
                wxLogError("Could not add script message handler");
            Slic3r::GUI::wxGetApp().set_adding_script_handler(false);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": finished add script message handler for wx.";
        };
#ifndef __WIN32__
        webView->CallAfter([webView, addScriptMessageHandler] {
#endif
            if (Slic3r::GUI::wxGetApp().is_adding_script_handler()) {
                g_delay_webviews.push_back(webView);
            } else {
                addScriptMessageHandler(webView);
                while (!g_delay_webviews.empty()) {
                    auto views = std::move(g_delay_webviews);
                    for (auto wv : views)
                        addScriptMessageHandler(wv);
                }
            }
#ifndef __WIN32__
        });
#endif
        webView->EnableContextMenu(true);
        // Roost Print: our fonts on every page the app shows from its own files.
        if (roost_web_theme_script().size() > 1)
            webView->AddUserScript(roost_web_theme_script(), wxWEBVIEW_INJECT_AT_DOCUMENT_START);
    } else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": failed. Use fake web view.";
        webView = new FakeWebView;
    }
    webView->SetRefData(new WebViewRef(webView));
    g_webviews.push_back(webView);
#ifdef __WIN32__
    if (use_single_process) {
        if (had_previous_args)
            wxSetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", previous_browser_args);
        else
            wxUnsetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");
    }
    if (!g_webview_atexit_registered) {
        g_webview_atexit_registered = true;
        std::atexit([] {
            BOOST_LOG_TRIVIAL(warning)
                << "[WebView][atexit] triggered, g_webviews.size()=" << g_webviews.size();
            WebView::BeginShutdown();
            WebView::FinalizeShutdown();
        });
    }
#endif
    return webView;
}
#if wxUSE_WEBVIEW_EDGE
void WebView::SetForceSingleProcess(bool force_single_process)
{
#ifdef __WIN32__
    g_force_single_process = force_single_process;
#else
    (void)force_single_process;
#endif
}
#endif
#if wxUSE_WEBVIEW_EDGE
bool WebView::CheckWebViewRuntime()
{
    wxWebViewFactoryEdge factory;
    auto wxVersion = factory.GetVersionInfo(wxVersionContext::RunTime);
    return wxVersion.GetMajor() != 0;
}
bool WebView::ReInstallWebViewRuntime()
{
    int returnCode = 2; // Download failed
    SHELLEXECUTEINFOW shExInfo = {0};
    //移除edge
    fs::path remove_edge_path = (fs::path(Slic3r::resources_dir()).parent_path() / "Remove-Edge.exe");
    //SHELLEXECUTEINFOW shExInfo = {0};
    shExInfo.cbSize = sizeof(shExInfo);
    shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shExInfo.hwnd = 0;
    shExInfo.lpVerb = L"runas";
    shExInfo.lpFile = remove_edge_path.generic_wstring().c_str();
    shExInfo.lpParameters = L" /install";
    shExInfo.lpDirectory = 0;
    shExInfo.nShow = 0;
    shExInfo.hInstApp = 0;

    if (ShellExecuteExW(&shExInfo)) {
      WaitForSingleObject(shExInfo.hProcess, INFINITE);
      returnCode = 0; // Install successfull
    } else {
      returnCode = 1; // remove failed
      return false;
    }
    //重新安装webview2 runtime
    fs::path target_file_path = (fs::path(Slic3r::resources_dir()).parent_path() / "MicrosoftEdgeWebView2RuntimeInstallerX64.exe");
    // Either Package the WebView2 Bootstrapper with your app or download it using fwlink
    // Then invoke install at Runtime.
    //SHELLEXECUTEINFOW shExInfo = {0};
    shExInfo.cbSize = sizeof(shExInfo);
    shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shExInfo.hwnd = 0;
    shExInfo.lpVerb = L"runas";
    shExInfo.lpFile = target_file_path.generic_wstring().c_str();
    shExInfo.lpParameters = L" /install";
    shExInfo.lpDirectory = 0;
    shExInfo.nShow = 0;
    shExInfo.hInstApp = 0;

    if (ShellExecuteExW(&shExInfo)) {
      WaitForSingleObject(shExInfo.hProcess, INFINITE);
      returnCode = 0; // Install successfull
    } else {
      returnCode = 1; // Install failed
    }
    //重新安装edge
    fs::path edge_file_path = (fs::path(Slic3r::resources_dir()).parent_path() / "MicrosoftEdgeSetup.exe");
    // Either Package the WebView2 Bootstrapper with your app or download it using fwlink
    // Then invoke install at Runtime.
    // 判断edge安装文件是否存在
    if (fs::exists(edge_file_path)) {
        shExInfo.cbSize = sizeof(shExInfo);
        shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
        shExInfo.hwnd = 0;
        shExInfo.lpVerb = L"runas";
        shExInfo.lpFile = edge_file_path.generic_wstring().c_str();
        shExInfo.lpParameters = L" /install";
        shExInfo.lpDirectory = 0;
        shExInfo.nShow = 0;
        shExInfo.hInstApp = 0;

        if (ShellExecuteExW(&shExInfo)) {
        WaitForSingleObject(shExInfo.hProcess, INFINITE);
        returnCode = 0; // Install successfull
        } else {
        returnCode = 1; // Install failed
        }
    }
    return returnCode == 0;
}
bool WebView::DownloadAndInstallWebViewRuntime()
{
    return DownloadAndInstallWV2RT() == 0;
}
#endif

bool WebView::ConfigureHardwareAccelerationForMjpeg(wxWebView *webView)
{
#if defined(__linux__)
    if (webView == nullptr)
        return false;

    const guint major = webkit_get_major_version();
    const guint minor = webkit_get_minor_version();
    const guint micro = webkit_get_micro_version();

    bool disable = major < 2 || (major == 2 && minor < 42);

    wxString override_value;
    if (wxGetEnv("CREALITY_WEBVIEW_HARDWARE_ACCELERATION", &override_value)) {
        override_value.Trim(true).Trim(false).MakeLower();
        if (override_value == "never")
            disable = true;
        else if (override_value == "default")
            disable = false;
        else if (!override_value.empty() && override_value != "auto") {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": ignoring invalid CREALITY_WEBVIEW_HARDWARE_ACCELERATION="
                                       << override_value.ToStdString() << " (expected auto, never, or default)";
        }
    }

    if (!disable) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": WebKitGTK " << major << "." << minor << "." << micro
                                << "; keeping default hardware acceleration";
        return true;
    }

    auto *native = static_cast<WebKitWebView *>(webView->GetNativeBackend());
    if (native == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": WebKit backend is not ready";
        return false;
    }

    WebKitSettings *settings = webkit_web_view_get_settings(native);
    if (settings == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": WebKit settings are unavailable";
        return false;
    }

    webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": WebKitGTK " << major << "." << minor << "." << micro
                            << "; hardware acceleration policy set to NEVER for MJPEG WebView";
    return true;
#else
    return false;
#endif
}

void WebView::LoadUrl(wxWebView * webView, wxString const &url)
{
    if (g_webview_shutting_down || webView == nullptr)
        return;

    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << url2.ToUTF8();
    webView->LoadURL(url2);
}

bool WebView::RunScript(wxWebView *webView, wxString const &javascript)
{
    if (g_webview_shutting_down || webView == nullptr)
        return false;

    if (Slic3r::GUI::wxGetApp().app_config->get("internal_developer_mode") == "true"
            && javascript.find("studio_userlogin") == wxString::npos)
        wxLogMessage("Running JavaScript:\n%s\n", javascript);

    try {
#ifdef __WIN32__
        ICoreWebView2 *   webView2 = (ICoreWebView2 *) webView->GetNativeBackend();
        if (webView2 == nullptr)
            return false;
        return webView2->ExecuteScript(javascript, NULL) == 0;
#elif defined __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_evaluateJavaScript(wkWebView, javascript, nullptr);
        return true;
#else
        WebKitWebView *wkWebView = (WebKitWebView *) webView->GetNativeBackend();
        webkit_web_view_run_javascript(
            wkWebView, javascript.utf8_str(), NULL,
            [](GObject *wkWebView, GAsyncResult *res, void *) {
                GError * error = NULL;
                auto result = webkit_web_view_run_javascript_finish((WebKitWebView*)wkWebView, res, &error);
                if (!result)
                    g_error_free (error);
                else
                    webkit_javascript_result_unref (result);
        }, NULL);
        return true;
#endif
    } catch (std::exception &e) {
        return false;
    }
}

bool WebView::IsShuttingDown()
{
    return g_webview_shutting_down;
}

void WebView::BeginShutdown()
{
    if (!g_webview_shutting_down) {
        g_webview_shutting_down = true;
        g_delay_webviews.clear();
    }

#ifdef __WIN32__
    // WebView2 initialization is asynchronous, so scan on every call and add
    // PIDs that were not available when shutdown first began. Do not Stop(),
    // navigate, clear g_webviews, wait, or terminate while controls are alive.
    for (auto* webView : g_webviews) {
        if (!webView)
            continue;
        auto* webView2 = reinterpret_cast<ICoreWebView2*>(webView->GetNativeBackend());
        if (!webView2)
            continue;

        UINT32 pid = 0;
        if (SUCCEEDED(webView2->get_BrowserProcessId(&pid)) && pid != 0)
            g_webview_browser_pids.insert(static_cast<DWORD>(pid));
    }
#endif

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": webviews=" << g_webviews.size()
#ifdef __WIN32__
                               << ", browser_pids=" << g_webview_browser_pids.size()
#endif
        ;
}

static bool wait_for_webview_browser_processes()
{
#ifdef __WIN32__
    if (g_webview_browser_pids.empty())
        return true;

    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (!g_webview_browser_pids.empty() && GetTickCount64() < deadline) {
        for (auto iter = g_webview_browser_pids.begin(); iter != g_webview_browser_pids.end();) {
            const DWORD pid = *iter;
            HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (handle) {
                const DWORD wait_result = WaitForSingleObject(handle, 0);
                CloseHandle(handle);
                if (wait_result == WAIT_OBJECT_0) {
                    iter = g_webview_browser_pids.erase(iter);
                    continue;
                }
            } else if (GetLastError() == ERROR_INVALID_PARAMETER) {
                // The process no longer exists.
                iter = g_webview_browser_pids.erase(iter);
                continue;
            }
            ++iter;
        }
        if (!g_webview_browser_pids.empty())
            Sleep(50);
    }

    for (auto iter = g_webview_browser_pids.begin(); iter != g_webview_browser_pids.end();) {
        const DWORD pid = *iter;
        HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!handle) {
            const DWORD error = GetLastError();
            if (error == ERROR_INVALID_PARAMETER) {
                iter = g_webview_browser_pids.erase(iter);
                continue;
            }
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": cannot terminate WebView2 browser process " << pid
                                       << ", error=" << error;
            ++iter;
            continue;
        }

        if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": terminating WebView2 browser process " << pid;
            if (!TerminateProcess(handle, 1))
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                           << ": TerminateProcess failed for " << pid
                                           << ", error=" << GetLastError();
        }
        const bool stopped = WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0;
        CloseHandle(handle);
        if (stopped) {
            iter = g_webview_browser_pids.erase(iter);
            continue;
        }
        ++iter;
    }

    return g_webview_browser_pids.empty();
#else
    return true;
#endif
}

void WebView::FinalizeShutdown()
{
    // Always rescan before finalization so a backend that completed after the
    // first shutdown request is still captured.
    BeginShutdown();

    // Releasing the shared environment or killing WebView2 while a wxWebView
    // still exists recreates the shutdown race this split is intended to fix.
    if (!g_webviews.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": deferred because "
                                   << g_webviews.size() << " webview(s) are still alive";
        return;
    }

#if wxUSE_WEBVIEW_EDGE
#ifdef __WIN32__
    // Drop the last shared environment reference before waiting for the
    // browser processes, but keep the profile marker until they are gone.
    g_webview_configuration.reset();
#endif
#endif
    const bool browser_processes_stopped = wait_for_webview_browser_processes();

#if wxUSE_WEBVIEW_EDGE
    if (browser_processes_stopped) {
        ReleaseConfiguration();
    } else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << ": preserving WebView2 profile marker because "
                                   << g_webview_browser_pids.size() << " process(es) remain";
    }
#endif
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " done";
}

#if wxUSE_WEBVIEW_EDGE
void WebView::ReleaseConfiguration()
{
#ifdef __WIN32__
    // Release the shared WebView2 environment before wxWidgets unloads its
    // WebView module. Leaving this to the CRT static destructors is too late.
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " releasing shared WebView2 configuration";
    g_webview_configuration.reset();

    // Only mark the profile reusable after the shared WebView2 environment is released.
    if (!g_webview_userdata_marker.empty()) {
        if (!wxRemoveFile(g_webview_userdata_marker))
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " failed to remove WebView2 profile marker: "
                                       << g_webview_userdata_marker.ToStdString();
        g_webview_userdata_marker.clear();
    }
    if (g_webview_userdata_ephemeral && !g_webview_userdata_dir.empty())
        wxFileName::Rmdir(g_webview_userdata_dir, wxPATH_RMDIR_RECURSIVE);
    g_webview_userdata_dir.clear();
    g_webview_userdata_ephemeral = false;
#endif
}
#endif

void WebView::RecreateAll()
{
    if (g_webview_shutting_down) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": ignored during application shutdown";
        return;
    }

    BOOST_LOG_TRIVIAL(warning) <<__FUNCTION__ << " start";
    auto dark = Slic3r::GUI::wxGetApp().dark_mode();
    for (auto webView : g_webviews) {
        void* backend_before = webView->GetNativeBackend();
        BOOST_LOG_TRIVIAL(warning) << "[RECREATE_ALL] Reloading webView " << (void*) webView << ". Backend BEFORE: " << backend_before;

        webView->SetUserAgent(wxString::Format("Creality-Slicer/v%s (%s) Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)", SLIC3R_VERSION,
                                               dark ? "dark" : "light"));
        webView->Reload();

        void* backend_after = webView->GetNativeBackend();
        BOOST_LOG_TRIVIAL(warning) << "[RECREATE_ALL] Reloaded webView " << (void*) webView << ". Backend AFTER: " << backend_after;

    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " end";
    boost::log::core::get()->flush();
}
