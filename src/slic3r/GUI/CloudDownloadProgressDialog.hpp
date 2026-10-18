#ifndef slic3r_CloudDownloadProgressDialog_hpp_
#define slic3r_CloudDownloadProgressDialog_hpp_

#include <wx/dialog.h>
#include <wx/simplebook.h>
#include <wx/timer.h>
#include <string>
#include <memory>

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "BBLStatusBarSend.hpp"

namespace Slic3r { namespace GUI {

class CloudDownloadProgressDialog : public DPIDialog
{
public:
    CloudDownloadProgressDialog(wxString title,
                                const std::string &user_id,
                                const std::string &file_id);
    ~CloudDownloadProgressDialog();

    bool Show(bool show) override;
    int ShowModal();
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void on_close(wxCloseEvent &event);
    void on_timer(wxTimerEvent &event);

private:
    std::string m_user_id;
    std::string m_file_id;
    wxSimplebook *m_simplebook_status { nullptr };
    std::shared_ptr<BBLStatusBarSend> m_status_bar;
    wxPanel *m_panel_download { nullptr };
    wxTimer m_timer;
    bool m_finished { false };
};

}}

#endif