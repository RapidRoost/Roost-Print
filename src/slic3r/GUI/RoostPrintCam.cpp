// Roost Print: PrintCam, built in. See RoostPrintCam.hpp.
#include "RoostPrintCam.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Widgets/WebView.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
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
