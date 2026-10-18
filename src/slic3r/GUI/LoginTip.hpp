#ifndef slic3r_LoginTip_hpp_
#define slic3r_LoginTip_hpp_
#include <vector>
#include <string>
#include <map>
#include <json_diff.hpp>

namespace Slic3r {
namespace GUI {

class LoginTip
{
public:
    static LoginTip& getInstance();
    // 1: user has presets, 0: user has no presets, wxID_YES: skip to login
    int isFilamentUserMaterialValid(const std::string& userMaterial);
    int showTokenInvalidTipDlg(const std::string& fromPage);
    void resetHasSkipToLogin();
    bool tokenInvalidHasTip() { return m_bTokenInvalidHasTip; }

    int syncShowTokenInvalidTipDlg(const std::string& fromPage);

private:
    int showAutoMappingNoLoginTipDlg(const std::string& fromPage);
    int showAutoMappingDiffAccountTipDlg(const std::string& fromPage);
    int showNoSelectedSyncUserPreset(const std::string& fromPage);

private:
    LoginTip();

private:
    bool m_bShowTipDlg = true;
    bool m_bHasSkipToLogin = false;
    bool m_bTokenInvalidHasTip = false;
};

}
}

#endif
