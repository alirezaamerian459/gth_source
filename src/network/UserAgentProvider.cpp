#include "UserAgentProvider.h"
#include <random>
#include <ctime>

const std::vector<std::string> UserAgentProvider::CHROME_UAS = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36",
};

const std::vector<std::string> UserAgentProvider::FIREFOX_UAS = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:132.0) Gecko/20100101 Firefox/132.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:131.0) Gecko/20100101 Firefox/131.0",
};

const std::vector<std::string> UserAgentProvider::SAFARI_UAS = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
};

std::string UserAgentProvider::GetRandomUserAgent() {
    static std::random_device rd;
    static std::mt19937 gen(rd() + std::time(nullptr));
    std::uniform_int_distribution<> dis(0, CHROME_UAS.size() - 1);
    return CHROME_UAS[dis(gen)];
}

std::string UserAgentProvider::GetChrome() {
    static std::random_device rd;
    static std::mt19937 gen(rd() + std::time(nullptr));
    std::uniform_int_distribution<> dis(0, CHROME_UAS.size() - 1);
    return CHROME_UAS[dis(gen)];
}

std::string UserAgentProvider::GetFirefox() {
    static std::random_device rd;
    static std::mt19937 gen(rd() + std::time(nullptr));
    std::uniform_int_distribution<> dis(0, FIREFOX_UAS.size() - 1);
    return FIREFOX_UAS[dis(gen)];
}

std::string UserAgentProvider::GetSafari() {
    static std::random_device rd;
    static std::mt19937 gen(rd() + std::time(nullptr));
    std::uniform_int_distribution<> dis(0, SAFARI_UAS.size() - 1);
    return SAFARI_UAS[dis(gen)];
}

std::string UserAgentProvider::GetForWindows10() {
    return GetChrome();
}

std::string UserAgentProvider::GetForWindows11() {
    return GetChrome();
}
