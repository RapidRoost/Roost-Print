// Roost Print: PrintCam, built in.
//
// PrintCam is Rapid Roost's own camera relay. It runs on the shop computer, watches the printers'
// cameras and tells rapidroost.net how each customer's print is going. It has its own page on this
// computer only (http://127.0.0.1:8788/).
//
// Roost Print doesn't carry its own copy (or any of its settings or keys). It finds the PrintCam
// that's installed on this computer, starts it in the background if it isn't running, and shows
// its page in a PrintCam tab, so there's no separate program to open.
#ifndef slic3r_RoostPrintCam_hpp_
#define slic3r_RoostPrintCam_hpp_

#include <string>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/timer.h>

class wxWebView;
class wxStaticText;

namespace Slic3r {
namespace GUI {
namespace RoostPrintCam {

// PrintCam's page, on this computer.
wxString page_url();

// PrintCam answers on its port right now.
bool is_running();

// Where PrintCam.exe is: the path saved in Roost Print's settings, else the one Windows starts at
// sign-in, else C:\PrintCam\PrintCam.exe, else next to Roost Print. Empty if none of those exist.
std::string find_exe();

// Remember where PrintCam.exe is (from the "Find PrintCam" button).
void set_exe(const std::string& path);

// Start PrintCam in the background (no browser window) if it's installed and not running.
// Returns true if it's running, or was started.
bool ensure_started();

} // namespace RoostPrintCam

// The PrintCam tab: PrintCam's page, or a note (with a "Find PrintCam.exe" button) when it isn't there.
class RoostPrintCamPanel : public wxPanel
{
public:
    explicit RoostPrintCamPanel(wxWindow* parent);
    // The tab was opened: start PrintCam if needed, then show its page.
    void on_show();
    void reload();

private:
    void show_missing(const wxString& why);
    void show_browser();
    void on_timer();

    wxWebView*    m_browser{nullptr};
    wxPanel*      m_missing{nullptr};
    wxStaticText* m_title{nullptr};
    wxStaticText* m_msg{nullptr};
    wxTimer       m_timer;
    int           m_tries{0};
    bool          m_loaded{false};
};
} // namespace GUI
} // namespace Slic3r

#endif
