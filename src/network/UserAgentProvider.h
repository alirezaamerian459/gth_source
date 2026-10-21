#pragma once

#include <string>
#include <vector>

class UserAgentProvider {
public:
    static std::string GetRandomUserAgent();
    static std::string GetChrome();
    static std::string GetFirefox();
    static std::string GetSafari();
    static std::string GetForWindows10();
    static std::string GetForWindows11();

private:
    static const std::vector<std::string> CHROME_UAS;
    static const std::vector<std::string> FIREFOX_UAS;
    static const std::vector<std::string> SAFARI_UAS;
};
