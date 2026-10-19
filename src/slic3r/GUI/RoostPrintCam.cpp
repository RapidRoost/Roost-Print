// Roost Print: PrintCam, built in. See RoostPrintCam.hpp.
#include "RoostPrintCam.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Widgets/WebView.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "BBLTopbar.hpp"
#include "NotificationManager.hpp"
#include "GLCanvas3D.hpp"
#include "format.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/Utils/Http.hpp"

#include <nlohmann/json.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <regex>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/dialog.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/webview.h>

#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {
namespace RoostPrintCam {

static const int  PORT = 8788;
static const char* SECTION = "roost";
static const char* KEY_EXE = "printcam_path";

wxString page_url() { return wxString::Format("http://127.0.0.1:%d/", PORT); }

bool is_running()
{
    try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::socket sock(io);
        bool ok = false;
        sock.async_connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), PORT),
                           [&ok](const boost::system::error_code& ec) { ok = !ec; });
        io.run_for(std::chrono::milliseconds(400));
        return ok;
    } catch (...) {
        return false;
    }
}

static bool exists(const std::string& p)
{
    boost::system::error_code ec;
    return !p.empty() && fs::is_regular_file(fs::path(p), ec);
}

#ifdef _WIN32
// The path PrintCam left for Windows to start it at sign-in: "C:\PrintCam\PrintCam.exe" --startup
static std::string from_windows_startup()
{
    wchar_t buf[2048];
    DWORD   size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"PrintCam", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    std::wstring v(buf);
    std::wstring exe;
    if (!v.empty() && v[0] == L'"') {
        size_t end = v.find(L'"', 1);
        exe = end == std::wstring::npos ? v.substr(1) : v.substr(1, end - 1);
    } else {
        exe = v.substr(0, v.find(L' '));
    }
    return wxString(exe).ToUTF8().data();
}
#endif

std::string find_exe()
{
    std::vector<std::string> tries;
    if (wxGetApp().app_config)
        tries.push_back(wxGetApp().app_config->get(SECTION, KEY_EXE));
#ifdef _WIN32
    tries.push_back(from_windows_startup());
    tries.push_back("C:\\PrintCam\\PrintCam.exe");
#endif
    fs::path here = fs::path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data()).parent_path();
    tries.push_back((here / "PrintCam" / "PrintCam.exe").string());
    tries.push_back((here / "PrintCam.exe").string());
    for (const auto& t : tries)
        if (exists(t))
            return t;
    return {};
}

void set_exe(const std::string& path)
{
    if (wxGetApp().app_config) {
        wxGetApp().app_config->set(SECTION, KEY_EXE, path);
        wxGetApp().app_config->save();
    }
}

bool ensure_started()
{
    if (is_running())
        return true;
    const std::string exe = find_exe();
    if (exe.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Roost Print: PrintCam not found";
        return false;
    }
    // In its own folder (that's where its settings live), with no browser window: Roost Print shows the page.
    wxExecuteEnv env;
    env.cwd = wxString::FromUTF8(fs::path(exe).parent_path().string());
    const wxString cmd = "\"" + wxString::FromUTF8(exe) + "\" -no-browser";
    const long pid = wxExecute(cmd, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, nullptr, &env);
    BOOST_LOG_TRIVIAL(info) << "Roost Print: started PrintCam (" << exe << "), pid " << pid;
    return pid > 0;
}


/* ----- Order numbers ----- */

static bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::vector<std::string> orders_in_name(const std::string& path)
{
    // Same rule as PrintCam (orders.go): only the file's own name counts; "RR" in any case, then
    // digits, with at most one '-', '_' or space between; not glued to the end of a word;
    // leading zeros dropped; more than 10 digits is no order; at most 8 orders.
    std::string base = path;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    static const std::regex order_re("[rR][rR][-_ ]?([0-9]+)");
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(base.begin(), base.end(), order_re); it != std::sregex_iterator(); ++it) {
        const auto pos = size_t(it->position(0));
        if (pos > 0 && is_letter(base[pos - 1]))
            continue;
        std::string digits = (*it)[1].str();
        digits.erase(0, digits.find_first_not_of('0'));
        if (digits.empty() || digits.size() > 10)
            continue;
        if (std::find(out.begin(), out.end(), digits) == out.end())
            out.push_back(digits);
        if (out.size() == 8)
            break;
    }
    return out;
}

std::string order_file_name(const std::string& name, const std::vector<std::string>& orders)
{
    // Keep the file type.
    static const char* exts[] = {".gcode.3mf", ".bgcode", ".gcode", ".3mf"};
    std::string stem = name, ext;
    for (const char* e : exts) {
        const size_t n = strlen(e);
        if (stem.size() > n && boost::iequals(stem.substr(stem.size() - n), e)) {
            ext  = stem.substr(stem.size() - n);
            stem = stem.substr(0, stem.size() - n);
            break;
        }
    }
    // Plain letters, digits, spaces, '_', '-' and '.'; anything else (accents too) becomes '_'.
    std::string clean;
    for (unsigned char c : stem) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
                        c == '-' || c == '.';
        const char out = ok ? char(c) : '_';
        if (out == '_' && !clean.empty() && clean.back() == '_')
            continue;
        clean += out;
    }
    while (!clean.empty() && (clean.back() == '_' || clean.back() == ' ' || clean.back() == '.'))
        clean.pop_back();
    while (!clean.empty() && (clean.front() == '_' || clean.front() == ' '))
        clean.erase(0, 1);
    if (clean.empty())
        clean = "print";
    // Put the missing order numbers in front.
    const auto have = orders_in_name(clean);
    std::string prefix;
    for (const auto& o : orders)
        if (std::find(have.begin(), have.end(), o) == have.end())
            prefix += "RR" + o + "_";
    return prefix + clean + ext;
}

// "1042", "RR1042 rr1043", "1042, 1043" -> {"1042", "1043"}
static std::vector<std::string> parse_typed_orders(const std::string& text)
{
    std::vector<std::string> out;
    static const std::regex num_re("[0-9]+");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), num_re); it != std::sregex_iterator(); ++it) {
        std::string d = it->str();
        d.erase(0, d.find_first_not_of('0'));
        if (!d.empty() && d.size() <= 10 && std::find(out.begin(), out.end(), d) == out.end())
            out.push_back(d);
    }
    if (out.size() > 8)
        out.resize(8);
    return out;
}

bool orders_for_plate(wxWindow* parent, int plate_index, std::vector<std::string>& orders)
{
    orders.clear();
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return true;
    // What the plate is called: the project, and the objects on it.
    std::vector<std::string> names{std::string(plater->get_project_name().ToUTF8().data())};
    if (PartPlate* plate = plater->get_partplate_list().get_plate(plate_index))
        for (const ModelObject* obj : plate->get_objects_on_this_plate())
            if (obj != nullptr)
                names.push_back(obj->name);
    std::string key = std::to_string(plate_index);
    for (const auto& n : names) {
        key += "|" + n;
        for (const auto& o : orders_in_name(n))
            if (std::find(orders.begin(), orders.end(), o) == orders.end())
                orders.push_back(o);
    }
    if (!orders.empty())
        return true; // the names already say which order(s)

    // Ask (with last time's answer for this same plate filled in).
    static std::map<std::string, std::string> answered;
    wxDialog dlg(parent, wxID_ANY, _L("Which order is this print for?"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* msg   = new wxStaticText(&dlg, wxID_ANY,
                                   _L("Type the order number, so the customer can watch their print on PrintCam.\n"
                                      "More than one order on this plate? Type them all: 1042 1043"));
    auto* field = new wxTextCtrl(&dlg, wxID_ANY, wxString::FromUTF8(answered.count(key) ? answered[key] : std::string()),
                                 wxDefaultPosition, wxSize(dlg.FromDIP(320), -1), wxTE_PROCESS_ENTER);
    field->SetHint(_L("Order number, e.g. 1042"));
    auto* error = new wxStaticText(&dlg, wxID_ANY, "");
    error->SetForegroundColour(wxColour(255, 127, 112));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* shop    = new wxButton(&dlg, wxID_NO, _L("Shop print, no order"));
    auto* cancel  = new wxButton(&dlg, wxID_CANCEL, _L("Cancel"));
    auto* ok      = new wxButton(&dlg, wxID_OK, _L("Send"));
    ok->SetDefault();
    buttons->Add(shop, 0, wxRIGHT, dlg.FromDIP(16));
    buttons->AddStretchSpacer(1);
    buttons->Add(cancel, 0, wxRIGHT, dlg.FromDIP(8));
    buttons->Add(ok, 0);
    sizer->Add(msg, 0, wxALL, dlg.FromDIP(16));
    sizer->Add(field, 0, wxLEFT | wxRIGHT | wxEXPAND, dlg.FromDIP(16));
    sizer->Add(error, 0, wxLEFT | wxRIGHT | wxTOP, dlg.FromDIP(16));
    sizer->Add(buttons, 0, wxALL | wxEXPAND, dlg.FromDIP(16));
    dlg.SetSizerAndFit(sizer);
    dlg.CentreOnParent();
    std::vector<std::string> typed;
    auto try_send = [&]() {
        typed = parse_typed_orders(field->GetValue().ToUTF8().data());
        if (typed.empty()) {
            error->SetLabel(_L("Type the order number, or press \"Shop print, no order\"."));
            dlg.Layout();
            dlg.Fit();
            return;
        }
        dlg.EndModal(wxID_OK);
    };
    ok->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { try_send(); });
    field->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) { try_send(); });
    shop->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { dlg.EndModal(wxID_NO); });
    field->SetFocus();
    const int res = dlg.ShowModal();
    if (res == wxID_CANCEL)
        return false;
    if (res == wxID_NO) {
        answered[key] = "";
        return true; // a shop print: no customer to tell
    }
    orders = typed;
    std::string joined;
    for (const auto& o : orders)
        joined += (joined.empty() ? "" : " ") + o;
    answered[key] = joined;
    BOOST_LOG_TRIVIAL(warning) << "Roost Print: plate " << plate_index + 1 << " is for order(s) " << joined;
    return true;
}

/* ----- Watching PrintCam ----- */

namespace {

struct Snapshot
{
    bool                     running{false};
    bool                     site_ok{true};
    std::string              site_error;
    std::vector<std::string> asks;        // orders waiting for a yes/no on their email
    std::vector<std::string> no_order;    // printers printing a file with no order number
    std::vector<std::string> unknown;     // order numbers the website doesn't know
};

static std::string join(const std::vector<std::string>& v, const std::string& sep = ", ")
{
    std::string s;
    for (const auto& x : v)
        s += (s.empty() ? "" : sep) + x;
    return s;
}

static Snapshot parse_state(const std::string& body)
{
    Snapshot snap;
    snap.running = true;
    const auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return snap;
    if (j.contains("sync") && j["sync"].is_object()) {
        const auto& sync = j["sync"];
        const bool tried = sync.value("last", int64_t(0)) > 0;
        snap.site_ok     = !tried || sync.value("ok", true);
        snap.site_error  = sync.value("error", std::string());
        if (sync.contains("unknown") && sync["unknown"].is_array())
            for (const auto& o : sync["unknown"])
                if (o.is_string())
                    snap.unknown.push_back(o.get<std::string>());
    }
    if (j.contains("emails") && j["emails"].is_array())
        for (const auto& e : j["emails"])
            if (e.is_object() && e.value("state", std::string()) == "ask")
                snap.asks.push_back(e.value("order", std::string()));
    if (j.contains("printers") && j["printers"].is_array())
        for (const auto& p : j["printers"]) {
            if (!p.is_object() || !p.contains("status") || !p["status"].is_object())
                continue;
            const std::string state = p["status"].value("state", std::string());
            const bool busy = state == "printing" || state == "paused" || state == "preparing";
            const bool has_order = p.contains("orders") && p["orders"].is_array() && !p["orders"].empty();
            if (busy && !has_order)
                snap.no_order.push_back(p.value("name", std::string("A printer")));
        }
    return snap;
}

class Watcher;
Watcher* g_watcher = nullptr;

class Watcher : public wxEvtHandler
{
public:
    Watcher() : m_timer(this)
    {
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { tick(); });
    }
    void start()
    {
        tick();
        m_timer.Start(8000);
    }
    void stop()
    {
        m_timer.Stop();
        m_stopping = true;
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    void tick()
    {
        if (m_busy || m_stopping)
            return;
        m_busy = true;
        if (m_thread.joinable())
            m_thread.join();
        m_thread = std::thread([this]() {
            std::string body;
            bool        ok = false;
            try {
                Http::get(page_url().ToStdString() + "api/state")
                    .timeout_connect(2)
                    .timeout_max(5)
                    .on_complete([&](std::string b, unsigned status) {
                        ok   = status == 200;
                        body = std::move(b);
                    })
                    .on_error([&](std::string, std::string, unsigned) { ok = false; })
                    .perform_sync();
            } catch (...) {
                ok = false;
            }
            wxGetApp().CallAfter([this, ok, body]() {
                if (g_watcher != this)
                    return; // stopped meanwhile
                m_busy = false;
                if (!m_stopping)
                    apply(ok ? parse_state(body) : Snapshot{});
            });
        });
    }

    void apply(const Snapshot& snap)
    {
        // PrintCam stopped: start it again, at most once a minute.
        bool installed = true;
        if (!snap.running) {
            const auto now = std::chrono::steady_clock::now();
            installed      = !find_exe().empty();
            if (installed && now - m_last_start > std::chrono::seconds(60)) {
                m_last_start = now;
                ensure_started();
            }
        }
        MainFrame* frame = wxGetApp().mainframe;
        if (frame == nullptr)
            return;
        // "PrintCam (2)" while customers wait for a yes/no.
        if (frame->topbar())
            frame->topbar()->SetTabText(MainFrame::tpPrintCam,
                                        snap.asks.empty() ? _L("PrintCam") : wxString::Format("%s (%d)", _L("PrintCam"), int(snap.asks.size())));

        Plater* plater = wxGetApp().plater();
        NotificationManager* nm = plater ? plater->get_notification_manager() : nullptr;
        if (nm == nullptr)
            return;
        auto open_printcam = [](wxEvtHandler*) {
            if (wxGetApp().mainframe)
                wxGetApp().mainframe->select_tab(size_t(MainFrame::tpPrintCam));
            return true;
        };

        // Customers waiting for their "your print started" email.
        const std::string asks = join(snap.asks);
        if (asks != m_shown_asks) {
            m_shown_asks = asks;
            nm->close_notification_of_type(NotificationType::RoostPrintCamAsk);
            if (!snap.asks.empty()) {
                const std::string text = snap.asks.size() == 1 ?
                    format(_u8L("Order %1% started printing. Email the customer their PrintCam link?"), asks) :
                    format(_u8L("Orders %1% started printing. Email the customers their PrintCam links?"), asks);
                nm->push_notification(NotificationType::RoostPrintCamAsk, NotificationManager::NotificationLevel::ImportantNotificationLevel,
                                      text, _u8L("Open PrintCam"), open_printcam);
            }
        }

        // Anything that stops customers watching.
        std::vector<std::string> problems;
        if (!snap.running)
            problems.push_back(installed ? _u8L("PrintCam isn't running, so customers can't watch their prints. Roost Print is starting it.") :
                                           _u8L("PrintCam isn't on this computer, so customers can't watch their prints."));
        else {
            if (!snap.site_ok)
                problems.push_back(_u8L("PrintCam can't reach rapidroost.net") + (snap.site_error.empty() ? "." : ": " + snap.site_error));
            if (!snap.no_order.empty())
                problems.push_back(format(_u8L("%1%: printing a file with no order number, so no customer can watch it."), join(snap.no_order)));
            if (!snap.unknown.empty())
                problems.push_back(format(_u8L("Order number %1% isn't on the website. Check it for a typo."), join(snap.unknown)));
        }
        const std::string text = join(problems, "\n");
        // A brief blip (one missed check) isn't worth a warning: wait for it twice in a row.
        if (text != m_pending_problem) {
            m_pending_problem = text;
            return;
        }
        if (text == m_shown_problem)
            return;
        m_shown_problem = text;
        nm->close_notification_of_type(NotificationType::RoostPrintCamWarning);
        if (!text.empty() && !(snap.running == false && !installed && m_warned_missing)) {
            if (!snap.running && !installed)
                m_warned_missing = true; // say "not on this computer" once, not every few seconds
            nm->push_notification(NotificationType::RoostPrintCamWarning, NotificationManager::NotificationLevel::WarningNotificationLevel,
                                  text, _u8L("Open PrintCam"), open_printcam);
        }
        if (auto* canvas = plater->get_current_canvas3D())
            canvas->schedule_extra_frame(0);
    }

    wxTimer     m_timer;
    std::thread m_thread;
    bool        m_busy{false};
    bool        m_stopping{false};
    bool        m_warned_missing{false};
    std::string m_shown_asks, m_shown_problem, m_pending_problem;
    std::chrono::steady_clock::time_point m_last_start{};
};

} // namespace

void start_watching()
{
    if (g_watcher == nullptr) {
        g_watcher = new Watcher();
        g_watcher->start();
    }
}

void stop_watching()
{
    if (g_watcher != nullptr) {
        Watcher* w = g_watcher;
        g_watcher  = nullptr; // anything still queued for it now does nothing
        w->stop();
        delete w;
    }
}

} // namespace RoostPrintCam

/* ----- the tab ----- */

RoostPrintCamPanel::RoostPrintCamPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY), m_timer(this)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(*wxWHITE);

    // Shown when PrintCam isn't there (or is still starting).
    m_missing = new wxPanel(this);
    m_missing->SetBackgroundColour(*wxWHITE);
    auto* ms = new wxBoxSizer(wxVERTICAL);
    m_title = new wxStaticText(m_missing, wxID_ANY, _L("PrintCam"));
    m_title->SetFont(m_title->GetFont().Scaled(1.6f).Bold());
    m_msg = new wxStaticText(m_missing, wxID_ANY, "");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* find = new wxButton(m_missing, wxID_ANY, _L("Find PrintCam.exe..."));
    auto* again = new wxButton(m_missing, wxID_ANY, _L("Try again"));
    buttons->Add(find, 0, wxRIGHT, FromDIP(10));
    buttons->Add(again, 0);
    ms->AddStretchSpacer(1);
    ms->Add(m_title, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(10));
    ms->Add(m_msg, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(18));
    ms->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL);
    ms->AddStretchSpacer(2);
    m_missing->SetSizer(ms);
    sizer->Add(m_missing, 1, wxEXPAND);
    SetSizer(sizer);

    find->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxFileDialog dlg(this, _L("Where is PrintCam.exe?"), "C:\\PrintCam", "PrintCam.exe", "PrintCam.exe|PrintCam.exe|Programs (*.exe)|*.exe",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;
        RoostPrintCam::set_exe(dlg.GetPath().ToUTF8().data());
        m_tries = 0;
        on_show();
    });
    again->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_tries = 0;
        on_show();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { on_timer(); });
}

void RoostPrintCamPanel::show_missing(const wxString& why)
{
    if (m_browser)
        m_browser->Hide();
    m_msg->SetLabel(why);
    m_msg->Wrap(FromDIP(520));
    m_missing->Show();
    Layout();
}

void RoostPrintCamPanel::show_browser()
{
    if (!m_browser) {
        m_browser = WebView::CreateWebView(this, RoostPrintCam::page_url());
        if (!m_browser) {
            show_missing(_L("This computer couldn't open a web view for PrintCam. Open http://127.0.0.1:8788/ in your browser instead."));
            return;
        }
        GetSizer()->Add(m_browser, 1, wxEXPAND);
    } else if (!m_loaded) {
        WebView::LoadUrl(m_browser, RoostPrintCam::page_url());
    }
    m_loaded = true;
    m_missing->Hide();
    m_browser->Show();
    Layout();
}

void RoostPrintCamPanel::on_show()
{
    if (RoostPrintCam::is_running()) {
        show_browser();
        return;
    }
    m_loaded = false;
    if (!RoostPrintCam::ensure_started()) {
        show_missing(_L("PrintCam isn't on this computer, or Roost Print can't find it.\n"
                        "It's usually in C:\\PrintCam. Press \"Find PrintCam.exe\" to show Roost Print where it is."));
        return;
    }
    // Started: give it a few seconds to come up.
    show_missing(_L("Starting PrintCam..."));
    m_tries = 0;
    m_timer.Start(500);
}

void RoostPrintCamPanel::on_timer()
{
    if (RoostPrintCam::is_running()) {
        m_timer.Stop();
        show_browser();
        return;
    }
    if (++m_tries > 40) {
        m_timer.Stop();
        show_missing(_L("PrintCam didn't start. Open it from its desktop shortcut once to see what it says, then press \"Try again\"."));
    }
}

void RoostPrintCamPanel::reload()
{
    if (m_browser && m_loaded)
        m_browser->Reload();
}

} // namespace GUI
} // namespace Slic3r
