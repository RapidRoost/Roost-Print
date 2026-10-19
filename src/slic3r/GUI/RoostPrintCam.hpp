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
#include <vector>
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

/* ----- Order numbers -----
 * PrintCam knows which customer a print is for from the print's file name: "RR" and the order
 * number, e.g. "RR1042_bracket.gcode" (two orders on one plate: "RR1042_RR1043_..."). Roost Print
 * makes sure every print it sends to a printer is named that way, so the customer always gets
 * their PrintCam link. Same rule as PrintCam's orders.go. */

// The order numbers in a name, in order, without repeats or leading zeros.
std::vector<std::string> orders_in_name(const std::string& name);

// A print name that's safe on Creality printers (letters, digits, spaces, '_', '-' and '.' only;
// apostrophes, accents, '#' and ';' have broken prints) and starts with the orders it's missing.
std::string order_file_name(const std::string& name, const std::vector<std::string>& orders);

// Before sending the current plate: which order(s) is it for? Uses the order numbers already in
// the project or object names, or the answer given last time for the same plate, or asks.
// An empty list means a shop print (no customer). False means the send was cancelled.
bool orders_for_plate(wxWindow* parent, int plate_index, std::vector<std::string>& orders);

/* ----- Watching PrintCam -----
 * While Roost Print runs it checks on PrintCam every few seconds (its page on this computer,
 * /api/state), and:
 *   - starts it again if it has stopped (at most once a minute),
 *   - shows "PrintCam (2)" on the tab while customers are waiting for a yes/no on their
 *     "your print has started" email, with a note in the 3D view that opens PrintCam,
 *   - warns when customers couldn't watch: PrintCam not running, the website not connected,
 *     a printer printing with no order number in its file, or an order number the website
 *     doesn't know (a typo). */
void start_watching();
void stop_watching();

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
