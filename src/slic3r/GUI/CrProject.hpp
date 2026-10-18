#ifndef slic3r_Creality_Project_hpp_
#define slic3r_Creality_Project_hpp_

#include "nlohmann/json.hpp"

namespace Slic3r {

    // {"application":"CubeMe","platform":"web","projectInfoId":"68ba597ebc603864d536e97e"}
    // Auxiliaries/metadata.json from cubeme project 3mf
    class MetaData {
    public:
        MetaData() = default;
        
        MetaData(std::string app, std::string plat, std::string projId)
            : m_application(std::move(app)),
            m_platform(std::move(plat)),
            m_projectInfoId(std::move(projId)) {}

        std::string getApplication() const { return m_application; }
        void setApplication(const std::string& app) { m_application = app; }

        std::string getPlatform() const { return m_platform; }
        void setPlatform(const std::string& plat) { m_platform = plat; }

        std::string getProjectInfoId() const { return m_projectInfoId; }
        void setProjectInfoId(const std::string& projId) { m_projectInfoId = projId; }

        void from_json(const nlohmann::json& j);

        void clear();

    private:
        std::string m_application = "";
        std::string m_platform = "";
        std::string m_projectInfoId = "";
    };


    // Auxiliaries directories from 3mf
    class AuxiliariesInfo {
    public:
        AuxiliariesInfo() {
            reset();
        }

        void reset();

        void reload(const std::string& auxiliary_path);

        std::string get_metadata_application();
        std::string get_metadata_platform();
        std::string get_metadata_project_id();

    private:
        MetaData meta_data;
    };

} // namespace Slic3r

#endif //  slic3r_Creality_Project_hpp_
