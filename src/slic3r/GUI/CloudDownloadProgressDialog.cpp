#include "CloudDownloadProgressDialog.hpp"
#include "MainFrame.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r { namespace GUI {

CloudDownloadProgressDialog::CloudDownloadProgressDialog(wxString title,
                                                         const std::string &user_id,
                                                         const std::string &file_id)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX, wxDialogNameStr)
    , m_user_id(user_id)
    , m_file_id(file_id)
    , m_timer(this)
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);

    m_simplebook_status = new wxSimplebook(this);
    m_simplebook_status->SetSize(wxSize(FromDIP(400), FromDIP(70)));
    m_simplebook_status->SetMinSize(wxSize(FromDIP(400), FromDIP(70)));
    m_simplebook_status->SetMaxSize(wxSize(FromDIP(400), FromDIP(70)));

    m_status_bar = std::make_shared<BBLStatusBarSend>(m_simplebook_status);
    m_panel_download = m_status_bar->get_panel();
    m_panel_download->SetSize(wxSize(FromDIP(400), FromDIP(70)));
    m_panel_download->SetMinSize(wxSize(FromDIP(400), FromDIP(70)));
    m_panel_download->SetMaxSize(wxSize(FromDIP(400), FromDIP(70)));

    m_sizer_main->Add(m_simplebook_status, 0, wxALL, FromDIP(20));
    m_sizer_main->Add(0, 0, 1, wxBOTTOM, 10);

    m_simplebook_status->AddPage(m_status_bar->get_panel(), wxEmptyString, true);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    CentreOnParent();

    Bind(wxEVT_CLOSE_WINDOW, &CloudDownloadProgressDialog::on_close, this);
    Bind(wxEVT_TIMER, &CloudDownloadProgressDialog::on_timer, this);
    wxGetApp().UpdateDlgDarkUI(this);
}

CloudDownloadProgressDialog::~CloudDownloadProgressDialog() {}

void CloudDownloadProgressDialog::on_dpi_changed(const wxRect &)
{
    Layout();
    Refresh();
}

bool CloudDownloadProgressDialog::Show(bool show)
{
    if (show) {
        m_simplebook_status->SetSelection(0);
        m_status_bar->set_status_text(_L("Downloading..."));
        m_status_bar->set_progress(0);

        // Cancel callback: cancel current download by fileId
        m_status_bar->set_cancel_callback_fina([this]() {
            wxGetApp().cancel_3mf_download(m_user_id, m_file_id);
            if (this->IsModal()) this->EndModal(wxID_CANCEL);
            else this->Close();
        });

        m_timer.Start(200);
    }
    return DPIDialog::Show(show);
}

int CloudDownloadProgressDialog::ShowModal()
{
    // Prepare UI state before entering modal loop
    m_simplebook_status->SetSelection(0);
    m_status_bar->set_status_text(_L("Downloading..."));
    m_status_bar->set_progress(0);

    // Cancel callback: cancel current download by fileId
    m_status_bar->set_cancel_callback_fina([this]() {
        wxGetApp().cancel_3mf_download(m_user_id, m_file_id);
        if (this->IsModal()) this->EndModal(wxID_CANCEL);
        else this->Close();
    });

    m_timer.Start(200);
    return DPIDialog::ShowModal();
}

void CloudDownloadProgressDialog::on_timer(wxTimerEvent &)
{
    if (m_finished) return;
    int percent = wxGetApp().get_3mf_download_progress(m_user_id, m_file_id);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    m_status_bar->set_progress(percent);

    if (percent >= 100) {
        m_finished = true;
        m_timer.Stop();

        // Retrieve the downloaded file path before closing, so we can trigger
        // import after the dialog is fully gone.
        std::string file_path = wxGetApp().get_3mf_download_path(m_user_id, m_file_id);

        // Close the dialog first, then schedule the import via CallAfter so
        // the download progress dialog is fully destroyed before the import
        // progress dialog appears — preventing two dialogs from overlapping.
        if (IsModal()) EndModal(wxID_CLOSE);
        else Close();

        if (!file_path.empty()) {
            wxTheApp->CallAfter([file_path]() {
                wxGetApp().request_model_download(wxString::FromUTF8(file_path.c_str()));
            });
        }
    }
}

void CloudDownloadProgressDialog::on_close(wxCloseEvent &event)
{
    if (m_timer.IsRunning()) m_timer.Stop();
    // If shown modally, ensure we exit the modal loop first so the parent
    // window is re-enabled correctly. Avoid destroying a stack-allocated
    // dialog while still in the modal loop.
    if (IsModal()) {
        EndModal(wxID_CANCEL);
        // Do not call Destroy() here for modal dialog; the caller owns
        // the object (often on stack) and will be cleaned up after ShowModal returns.
    } else {
        Destroy();
    }
}

}} // namespace Slic3r::GUI