#pragma once

#include <string>
#include <map>
#include <vector>
#include <ctime>

class CookieManager {
public:
    static void LoadCookies(const std::string& filePath);
    static void SaveCookies(const std::string& filePath);

    static void AddCookie(
        const std::string& name,
        const std::string& value,
        const std::string& domain = "",
        const std::string& path = "/",
        bool secure = true,
        std::time_t expires = 0
    );

    static std::string GetCookieHeader(const std::string& domain);
    static void Clear();

private:
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path;
        std::time_t expires;
        bool secure;
    };

    static std::map<std::string, std::vector<Cookie>>& GetCookieJar();
};
