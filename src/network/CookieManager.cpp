#include "CookieManager.h"
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <ctime>

std::map<std::string, std::vector<CookieManager::Cookie>>& CookieManager::GetCookieJar() {
    static std::map<std::string, std::vector<Cookie>> jar;
    return jar;
}

void CookieManager::LoadCookies(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        spdlog::debug("Cookies file not found: {}", filePath);
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string domain, path, name, value, expiresStr, secureStr;

        if (std::getline(iss, domain, '\t') &&
            std::getline(iss, path, '\t') &&
            std::getline(iss, name, '\t') &&
            std::getline(iss, value, '\t') &&
            std::getline(iss, expiresStr, '\t') &&
            std::getline(iss, secureStr)) {

            std::time_t expires = std::stol(expiresStr);
            bool secure = (secureStr == "1");

            AddCookie(name, value, domain, path, secure, expires);
        }
    }

    spdlog::info("Loaded cookies from {}", filePath);
}

void CookieManager::SaveCookies(const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        spdlog::error("Cannot open cookies file for writing: {}", filePath);
        return;
    }

    auto& jar = GetCookieJar();
    for (const auto& [domain, cookies] : jar) {
        for (const auto& cookie : cookies) {
            file << cookie.domain << "\t"
                 << cookie.path << "\t"
                 << cookie.name << "\t"
                 << cookie.value << "\t"
                 << cookie.expires << "\t"
                 << (cookie.secure ? "1" : "0") << "\n";
        }
    }

    spdlog::debug("Saved cookies to {}", filePath);
}

void CookieManager::AddCookie(
    const std::string& name,
    const std::string& value,
    const std::string& domain,
    const std::string& path,
    bool secure,
    std::time_t expires) {

    auto& jar = GetCookieJar();

    Cookie cookie;
    cookie.name = name;
    cookie.value = value;
    cookie.domain = domain;
    cookie.path = path;
    cookie.secure = secure;
    cookie.expires = expires;

    if (jar.find(domain) == jar.end()) {
        jar[domain] = std::vector<Cookie>();
    }

    jar[domain].push_back(cookie);
    spdlog::debug("Added cookie: {}={} for domain {}", name, value, domain);
}

std::string CookieManager::GetCookieHeader(const std::string& domain) {
    auto& jar = GetCookieJar();
    std::string header = "";

    if (jar.find(domain) != jar.end()) {
        std::time_t now = std::time(nullptr);

        for (const auto& cookie : jar[domain]) {
            if (cookie.expires > 0 && cookie.expires < now) {
                continue;
            }

            if (!header.empty()) header += "; ";
            header += cookie.name + "=" + cookie.value;
        }
    }

    return header;
}

void CookieManager::Clear() {
    GetCookieJar().clear();
    spdlog::info("Cleared all cookies");
}
