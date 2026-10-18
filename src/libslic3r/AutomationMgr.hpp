#ifndef _AUTOMATIONMGR_H
#define _AUTOMATIONMGR_H
#include <string>
#include <vector>

namespace Slic3r 
{
    class DynamicPrintConfig;
    class Model;
    class AutomationMgr
    {
        enum AutomationType {
            None = 0,
            GCode,
            Scale,
        };

        public:
            static std::string    g_3mfPath;
            static AutomationType g_automationType;

        public:
            static bool        enabled();
            static void        set3mfPath(const std::string& path);
            static void        setFuncType(int type);
            static std::string get3mfPath();
            static void        endFunction();
            static std::string getFileName();
            static void        outputLog(const std::string& logContent, const int& logType);
            static std::string getCurrentTime();
            static void        exportPrintConfigToJson(const Model& model, const DynamicPrintConfig& config);
            static std::string getTimeStamp();
            static std::string generateUniqueFilename(const std::string& basePath, const std::string& baseName, const std::string& extension);
    };
} // namespace Slic3r
#endif // _AUTOMATIONMGR_H
