#ifndef RemotePrint_DeviceListRoutes_hpp_
#define RemotePrint_DeviceListRoutes_hpp_
#include "Routes.hpp"

class wxWebView;
namespace DM{

    class DeviceMgrRoutes:public Routes
{
public:
    DeviceMgrRoutes();

private:
    void check_and_send_print_failure_events(const nlohmann::json& json_data);
};

#endif /* RemotePrint_DeviceDB_hpp_ */
}