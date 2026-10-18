#ifndef slic3r_SystemId_hpp_
#define slic3r_SystemId_hpp_

#include <string>

namespace Slic3r {
namespace GUI {
	class SystemId
	{
		public:
			static std::string get_system_id();

		private:
            static std::string get_raw_system_id();
            static std::string hash_with_salt(const std::string& raw);

		private:
            static constexpr const char* SALT = "CREALITY_DAU_V1_PRIVATE_SALT";
		};

	}// GUI
} // namespace Slic3r::GUI

#endif