#include "CrProject.hpp"
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>

using namespace nlohmann;

namespace pt = boost::property_tree;
namespace fs = boost::filesystem;

//Auxiliaries/metadata.json from cubeme project 3mf
#define META_DATA_JSON  "metadata.json"

namespace Slic3r {

    void MetaData::clear()
    {
        m_application.clear();
        m_platform.clear();
        m_projectInfoId.clear();
    }

    void MetaData::from_json(const nlohmann::json& j) 
    {
        j.at("application").get_to(m_application);
        j.at("platform").get_to(m_platform);
        j.at("projectInfoId").get_to(m_projectInfoId);
    }

    void AuxiliariesInfo::reset()
    {
        meta_data.clear();
    }

    void AuxiliariesInfo::reload(const std::string& auxiliary_path)
    {
        try
        {
            // Check auxiliary path.
            if (!fs::exists(auxiliary_path)) {
                meta_data.clear();
                return;
            }

            std::filesystem::path meta_data_file = (std::filesystem::path(auxiliary_path) / META_DATA_JSON)
                                                           .make_preferred();

            std::ifstream file(meta_data_file);
            if (!file.is_open()) {
                meta_data.clear();
                return;
            }

            json j;
            file >> j;
            file.close();

            meta_data.from_json(j);
        }
        catch (const std::exception&)
        {
            meta_data.clear();
        }
        catch(...)
        {
            meta_data.clear();
        }

    }

    std::string AuxiliariesInfo::get_metadata_application()
    {
        return meta_data.getApplication();
    }

    std::string AuxiliariesInfo::get_metadata_platform()
    {
        return meta_data.getPlatform();
    }

    std::string AuxiliariesInfo::get_metadata_project_id()
    {
        return meta_data.getProjectInfoId();
    }

} // namespace Slic3r
