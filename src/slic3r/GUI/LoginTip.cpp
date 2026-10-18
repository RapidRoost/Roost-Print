#include "LoginTip.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "CommunicateWithCXCloud.hpp"

namespace Slic3r {
namespace GUI {

LoginTip::LoginTip() {}

LoginTip& LoginTip::getInstance()
{
    static LoginTip instance;
    return instance;
}

int LoginTip::isFilamentUserMaterialValid(const std::string& userMaterial)
{
    int ret = 0;
    BOOST_LOG_TRIVIAL(warning) << "LoginTip isFilamentUserMaterialValid userMaterial=" << userMaterial;
    if (!userMaterial.empty()) {
        if (!wxGetApp().get_user().bLogin) {
            if (!m_bShowTipDlg || m_bHasSkipToLogin) {
                return wxID_CANCEL;
            }
            ret = showAutoMappingNoLoginTipDlg("");
            if (ret != wxID_YES) {
                m_bShowTipDlg = false;
            } else {
                m_bHasSkipToLogin = true;
            }
            return ret;
        }
        std::vector<std::string> vtUserMaterial;
        boost::split(vtUserMaterial, userMaterial, boost::is_any_of("/"));
        if (vtUserMaterial.size() < 2) {
            if (!m_bShowTipDlg || m_bHasSkipToLogin) {
                return wxID_CANCEL;
            }
            ret = showAutoMappingDiffAccountTipDlg("");
            if (ret != wxID_YES) {
                m_bShowTipDlg = false;
            } else {
                m_bHasSkipToLogin = true;
            }
            return ret;
        } else {
            std::string userId = vtUserMaterial[vtUserMaterial.size() - 2];
            if (userId != wxGetApp().get_user().userId) {
                if (!m_bShowTipDlg || m_bHasSkipToLogin) {
                    return wxID_CANCEL;
                }
                ret = showAutoMappingDiffAccountTipDlg("");
                if (ret != wxID_YES) {
                    m_bShowTipDlg = false;
                } else {
                    m_bHasSkipToLogin = true;
                }
                return ret;
            }
            
        }
        if (wxGetApp().app_config->get("sync_user_preset") != "true") {
            ret = showNoSelectedSyncUserPreset("");
            if (ret != wxID_YES) {
                m_bShowTipDlg = false;
            }
            return ret;
        }
        if (!CXCloudDataCenter::getInstance().isTokenValid()) {
            CommunicateWithCXCloud           commWithCXCloud;
            std::vector<UserProfileListItem> vtUserProfileListItem;
            commWithCXCloud.getUserProfileList(vtUserProfileListItem);
            if (!CXCloudDataCenter::getInstance().isTokenValid()) {
                if (!m_bShowTipDlg || m_bHasSkipToLogin) {
                    return wxID_CANCEL;
                }
                ret = syncShowTokenInvalidTipDlg("");
                if (ret != wxID_YES) {
                    m_bShowTipDlg = false;
                } else {
                    m_bHasSkipToLogin = true;
                }
                return ret;
            }
        }
        return 0;   // User preset is invalid
    }
    
    return 1;   // User has no preset
}

int LoginTip::showTokenInvalidTipDlg(const std::string& fromPage)
{
    wxGetApp().CallAfter([=] {
        wxString   strTip = _L("login status has expired, please log in again?");
        MessageDialog msgDlg(nullptr, strTip, wxEmptyString, wxICON_QUESTION | wxYES_NO);
        int           res = msgDlg.ShowModal();
        if (res == wxID_YES) {
            wxGetApp().mainframe->select_tab(MainFrame::tpHome);
            //wxGetApp().swith_community_sub_page("param_set");
        }
    });
    m_bTokenInvalidHasTip = true;
    return 0;
}

void LoginTip::resetHasSkipToLogin() { m_bHasSkipToLogin = false; }

int LoginTip::syncShowTokenInvalidTipDlg(const std::string& fromPage)
{
    wxString      strTip = _L("login status has expired, please log in again?");
    MessageDialog msgDlg(nullptr, strTip, wxEmptyString, wxICON_QUESTION | wxYES_NO);
    int           res = msgDlg.ShowModal();
    if (res == wxID_YES) {
        wxGetApp().mainframe->select_tab(MainFrame::tpHome);
        // wxGetApp().swith_community_sub_page("param_set");
    }
    return res;
}
int LoginTip::showAutoMappingNoLoginTipDlg(const std::string& fromPage)
{
    wxString      strTip = _L("Not logged in yet, unable to map user filament preset");
    AutoMappingLoginMsgDialog msgDlg(nullptr, strTip, wxEmptyString, wxICON_QUESTION | wxCANCEL | wxYES);
    int           res = msgDlg.ShowModal();
    if (res == wxID_YES) {
        wxGetApp().mainframe->select_tab(MainFrame::tpHome);
        // wxGetApp().swith_community_sub_page("param_set");
    }
    return res;
}
int LoginTip::showAutoMappingDiffAccountTipDlg(const std::string& fromPage)
{
    wxString      strTip = _L("The current account does not match the device account, and cannot map the user filament preset");

    AutoMappingLoginMsgDialog msgDlg(nullptr, strTip, wxEmptyString, wxICON_QUESTION | wxCANCEL | wxYES);
    int           res = msgDlg.ShowModal();
    if (res == wxID_YES) {
        wxGetApp().mainframe->select_tab(MainFrame::tpHome);
        // wxGetApp().swith_community_sub_page("param_set");
    }
    return res;
}

int LoginTip::showNoSelectedSyncUserPreset(const std::string& fromPage)
{
    wxString      strTip = _L("no selected sync user presets, please select again?");
    MessageDialog msgDlg(nullptr, strTip, wxEmptyString, wxICON_QUESTION | wxYES_NO);
    int           res = msgDlg.ShowModal();
    if (res == wxID_YES) {
        wxGetApp().app_config->set("sync_user_preset", "true");
        wxGetApp().start_sync_user_preset();
    }
    return res;
}


}
}
