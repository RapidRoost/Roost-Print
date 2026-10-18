// [FORMATTED BY CLANG-FORMAT 2025-10-24 10:54:26]
#include "ModelLibraryRoutes.hpp"
#include "nlohmann/json.hpp"
#include "../AppUtils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/MainFrame.hpp"

namespace fs = boost::filesystem;

using namespace Slic3r::GUI;

namespace DM {
ModelLibraryRoutes::ModelLibraryRoutes()
{
    // Handle model download start

    // Handle 3mf download start
    this->Handler({"3mf_download_start"},
                  [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
                      auto response = wxGetApp().handle_web_request(data);
                      if (!response.empty()) {
                          nlohmann::json commandJson;
                          commandJson["command"] = "3mf_download_start";
                          commandJson["data"]    = response;
                          AppUtils::PostMsg(browse, commandJson);
                      }
                      wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
                      return true;
                  });

    // Handle download state query
    this->Handler({"models_download_state"},
                  [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
                      auto response = wxGetApp().handle_web_request(data);
                      if (!response.empty()) {
                          try {
                              // Response is a JSON string produced by GUI_App::handle_web_request
                              nlohmann::json responseJson = nlohmann::json::parse(response);
                              // Forward the exact JSON so frontend listeners receive top-level fields
                              AppUtils::PostMsg(browse, responseJson);
                          } catch (...) {
                              // Fallback: preserve previous behavior if parsing fails
                              nlohmann::json commandJson;
                              commandJson["command"] = "models_download_state";
                              commandJson["data"]    = response;
                              AppUtils::PostMsg(browse, commandJson);
                          }
                      }
                      return true;
                  });

    // Handle download deletion
    this->Handler({"models_download_delete"},
                  [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
                      auto           response = wxGetApp().handle_web_request(data);
                      nlohmann::json commandJson;
                      commandJson["command"] = "models_download_delete";
                      commandJson["data"]    = response;
                      AppUtils::PostMsg(browse, commandJson);
                      return true;
                  });

    // Handle account info requests
    this->Handler({"get_account_info"},
                  [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
                      // Process account info directly in Routes system instead of calling handle_web_request
                      auto           user_file = fs::path(Slic3r::data_dir()).append("user_info.json");
                      nlohmann::json m_Res     = nlohmann::json::object();
                      m_Res["command"]         = "get_account_info";

                      // Include sequence_id from original request if present
                      if (json_data.contains("sequence_id")) {
                          m_Res["sequence_id"] = json_data["sequence_id"];
                      }

                      nlohmann::json j;
                      if (fs::exists(user_file)) {
                          try {
                              boost::nowide::ifstream ifs(user_file.string());
                              ifs >> j;
                          } catch (const std::exception& e) {
                              BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", parse json failed, %1%.") % e.what();
                          }
                      }

                      m_Res["account"] = nlohmann::json::object();
                      if (j.contains("token")) {
                          m_Res["account"]["token"] = j["token"];
                      }
                      if (j.contains("nickName")) {
                          m_Res["account"]["nickName"] = j["nickName"];
                      }
                      if (j.contains("avatar")) {
                          m_Res["account"]["avatar"] = j["avatar"];
                      }
                      if (j.contains("userId")) {
                          m_Res["account"]["userId"] = j["userId"];
                      }
                      if (j.contains("region")) {
                          m_Res["account"]["region"] = j["region"];
                      }

                      // Send response through Routes system using AppUtils::PostMsg
                      AppUtils::PostMsg(browse, m_Res);
                      return true;
                  });

    // Handle account updates
    this->Handler({"update_account_info"},
                  [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
                      auto           response = wxGetApp().handle_web_request(data);
                      return true;
                  });

    // Handle login success
    this->Handler({"login_account_success"},
                  [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
                      auto           response = wxGetApp().handle_web_request(data);
                      return true;
                  });

    // Handle client ID requests
    this->Handler({"get_client_id"}, [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
        // Directly get client_id and send response through Routes system
        nlohmann::json response;
        response["command"]   = "get_client_id";
        response["client_id"] = wxGetApp().get_client_id();

        // Include sequence_id from original request if present
        if (json_data.contains("sequence_id")) {
            response["sequence_id"] = json_data["sequence_id"];
        }

        AppUtils::PostMsg(browse, response);
        return true;
    });

    // Generic fallback handler for any unhandled command
    this->Handler({""}, [](wxWebView* browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
        // Only handle if no other handler matched and it's not already handled by other routes
        //auto response = wxGetApp().handle_web_request(data);
        //if (!response.empty()) {
        //    nlohmann::json commandJson;
        //    commandJson["command"] = cmd;
        //    commandJson["data"]    = response;
        //    // Include sequence_id from original request if present
        //    if (json_data.contains("sequence_id")) {
        //        commandJson["sequence_id"] = json_data["sequence_id"];
        //    }
        //    AppUtils::PostMsg(browse, commandJson);
        //}
        return true;
    });
}
} // namespace DM