#ifndef slic3r_AppUpdateDialogs_hpp_
#define slic3r_AppUpdateDialogs_hpp_

#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <cstddef>
#include "GUI_Utils.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/Button.hpp"

namespace Slic3r {
namespace GUI {

class AppUpdateProgressDialog : public DPIDialog
{
public:
    AppUpdateProgressDialog(wxWindow* parent);
    ~AppUpdateProgressDialog() override;
    
    void update_progress(int percent, const wxString& msg);
    bool Show(bool show = true) override;
    void close_imgui_notification();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    
private:
    void apply_theme();

    wxWindow* m_title_bar{ nullptr };
    wxWindow* m_progress_panel{ nullptr };
    ProgressBar* m_progress;
    wxStaticText* m_status_text;
    Button* m_cancel_btn;
    int m_last_percent;
    wxString m_last_msg;

    void show_imgui_notification();
    void restore_from_imgui_notification();
    static size_t imgui_notification_id();
};

class AppUpdateFinishDialog : public DPIDialog
{
public:
    AppUpdateFinishDialog(wxWindow* parent);
    bool Show(bool show = true) override;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void apply_theme();

    wxWindow* m_title_bar{ nullptr };
    wxStaticText* m_text{ nullptr };
    Button* m_btn_later{ nullptr };
    Button* m_btn_now{ nullptr };
};

class AppUpdateErrorDialog : public DPIDialog
{
public:
    AppUpdateErrorDialog(wxWindow* parent);
    void SetErrorMsg(const wxString& msg);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
private:
    wxStaticText* m_text;
};

}
}

#endif
