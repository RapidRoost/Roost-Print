#ifndef slic3r_UpdateParams_hpp_
#define slic3r_UpdateParams_hpp_
#include <vector>
#include <string>
#include <map>
#include <json_diff.hpp>

namespace Slic3r {
namespace GUI {

namespace nsUpdateParams {
class CXCloud
{
public:
    CXCloud()  = default;
    ~CXCloud() = default;

    int getNeedUpdatePrintes(std::vector<std::string>& vtNeedUpdatePrinter);
    void clearLocalPrinter();

private:
    int loadLocalPrinter();

private:
    std::map<std::string, std::string> m_mapLocalPrinterVersion;
    std::map<std::string, std::string> m_mapRemotePrinterVersion;
    bool m_bHasRequestedCXCloud = false;
};
} // namespace nsUpdateParams

class UpdateParams
{
public:
    static UpdateParams& getInstance();

    void checkParamsNeedUpdate();
    void closeParamsUpdateTip();
    void hasUpdateParams();

private:
    UpdateParams();

    void getCurrentPrinter(std::vector<std::string>& vtCurPrinter);

private:
    std::vector<std::string> m_vtNeedUpdatePrinter;
    nsUpdateParams::CXCloud m_CXCloud;
    bool m_bHasUpdateParams = false;
};

}
}

#endif
