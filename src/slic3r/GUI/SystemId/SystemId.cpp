#include <string>
#include "SystemId.hpp"

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <cstdio>
#elif defined(__linux__)
    #include <fstream>
#endif

#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

std::string Slic3r::GUI::SystemId::get_system_id()
{
    std::string raw = get_raw_system_id();
    if (raw.empty())
        return "";
    return hash_with_salt(raw);
}

std::string Slic3r::GUI::SystemId::get_raw_system_id()
{

#if defined(_WIN32)

    // Windows: MachineGuid
    HKEY hKey;
    char value[256] = {0};
    DWORD size = sizeof(value);

    if (RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Cryptography",
            0,
            KEY_READ | KEY_WOW64_64KEY,
            &hKey) == ERROR_SUCCESS) {

        if (RegQueryValueExA(
                hKey,
                "MachineGuid",
                nullptr,
                nullptr,
                reinterpret_cast<LPBYTE>(value),
                &size) == ERROR_SUCCESS) {

            RegCloseKey(hKey);
            return std::string(value);
        }

        RegCloseKey(hKey);
    }

    return "";

#elif defined(__APPLE__)

    // macOS: IOPlatformUUID
    FILE* fp = popen(
        "ioreg -rd1 -c IOPlatformExpertDevice | grep IOPlatformUUID",
        "r"
    );
    if (!fp) return "";

    char buffer[256];
    std::string result;

    if (fgets(buffer, sizeof(buffer), fp)) {
        result = buffer;

        auto first = result.find('\"');
        auto last  = result.rfind('\"');

        if (first != std::string::npos && last != std::string::npos && last > first) {
            result = result.substr(first + 1, last - first - 1);
        }
    }

    pclose(fp);
    return result;

#elif defined(__linux__)

    // Linux: /etc/machine-id
    std::ifstream file("/etc/machine-id");
    std::string id;

    if (file.is_open()) {
        file >> id;
        return id;
    }

    return "";

#else

    return "";

#endif
}

std::string Slic3r::GUI::SystemId::hash_with_salt(const std::string& raw)
{
    std::string input = raw + SALT;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int) hash[i];
    return oss.str();
}
