# GTH Bot Evasion Guide - Make GTH Pass Robot Detection Tests
**Implement bot evasion techniques from CapSkip & Appointment Bot**

---

## Goal
Add bot evasion capabilities to GTH so it can:
1. Pass CAPTCHA challenges (via CapSkip integration)
2. Evade automation detection (via Selenium techniques)
3. Bypass Cloudflare/WAF protections
4. Masquerade as legitimate user
5. Handle anti-scraping systems

---

## Part 1: CapSkip Integration (CAPTCHA Evasion)

### What GTH Needs
If GTH downloads/verifies games from online sources that have CAPTCHA:

### Implementation

#### Step 1: Create CapSkip Client Wrapper
```cpp
// src/captcha/CapSkipClient.h
#pragma once

#include <string>
#include <curl/curl.h>

class CapSkipClient {
public:
    CapSkipClient();
    ~CapSkipClient();
    
    // Solve reCAPTCHA v2
    std::string SolveRecaptchaV2(
        const std::string& sitekey,
        const std::string& pageurl
    );
    
    // Solve reCAPTCHA v3
    std::string SolveRecaptchaV3(
        const std::string& sitekey,
        const std::string& pageurl,
        double minScore = 0.7
    );
    
    // Solve Cloudflare Turnstile
    std::string SolveTurnstile(
        const std::string& sitekey,
        const std::string& pageurl
    );
    
    // Solve image CAPTCHA
    std::string SolveImageCaptcha(
        const std::string& imagePath
    );
    
private:
    std::string apiHost_;      // "127.0.0.1"
    int apiPort_;              // 8080
    
    bool IsCapSkipRunning();
    std::string PostToCapSkip(const std::string& endpoint, const std::string& data);
    std::string PollResult(const std::string& captchaId);
};
```

#### Step 2: Implement CapSkip Client
```cpp
// src/captcha/CapSkipClient.cpp
#include "CapSkipClient.h"
#include <thread>
#include <chrono>

CapSkipClient::CapSkipClient() : apiHost_("127.0.0.1"), apiPort_(8080) {
    if (!IsCapSkipRunning()) {
        // Auto-launch CapSkip if not running (optional)
        // system("start CapSkipInstaller.msi");
    }
}

bool CapSkipClient::IsCapSkipRunning() {
    // Check if localhost:8080 is listening
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    std::string url = "http://127.0.0.1:8080/api/version";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK);
}

std::string CapSkipClient::SolveRecaptchaV2(
    const std::string& sitekey,
    const std::string& pageurl) {
    
    std::string payload = "method=userrecaptcha&googlekey=" + sitekey 
                        + "&pageurl=" + pageurl;
    
    std::string captchaId = PostToCapSkip("/in.php", payload);
    return PollResult(captchaId);
}

std::string CapSkipClient::SolveTurnstile(
    const std::string& sitekey,
    const std::string& pageurl) {
    
    std::string payload = "method=turnstile&sitekey=" + sitekey 
                        + "&pageurl=" + pageurl;
    
    std::string captchaId = PostToCapSkip("/in.php", payload);
    return PollResult(captchaId);
}

std::string CapSkipClient::PollResult(const std::string& captchaId) {
    // Poll /res.php?captchaid=XXX until solved
    for (int i = 0; i < 60; i++) {  // 60 second timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        CURL* curl = curl_easy_init();
        std::string url = "http://127.0.0.1:8080/res.php?captchaid=" + captchaId;
        
        // Make request and parse response
        // If response contains the token, return it
        // If response is "CAPCHA_NOT_READY", continue polling
    }
    
    return "";  // Timeout
}
```

#### Step 3: Use in Download Service
```cpp
// src/application/DownloadManager.cpp - Add captcha solving
#include "captcha/CapSkipClient.h"

HttpResponse DownloadManager::DownloadWithCaptchaBypass(
    const std::string& url) {
    
    CURL* curl = curl_easy_init();
    
    // First attempt
    CURLcode res = curl_easy_perform(curl);
    
    // Check if Cloudflare/CAPTCHA blocked us
    if (IsCloudflareChallenge(response)) {
        LOG_INFO("Cloudflare challenge detected, solving...");
        
        CapSkipClient capskip;
        std::string sitekey = ExtractSitekey(response);
        std::string token = capskip.SolveTurnstile(sitekey, url);
        
        // Retry with CAPTCHA token
        curl_easy_setopt(curl, CURLOPT_COOKIE, token.c_str());
        res = curl_easy_perform(curl);
    }
    
    return response;
}
```

---

## Part 2: User Agent & Header Spoofing

### Problem
Anti-bot systems check for legitimate user agents and headers.

### Solution: Realistic User Agent Rotation

```cpp
// src/network/UserAgentProvider.h
#pragma once
#include <string>
#include <vector>

class UserAgentProvider {
public:
    static std::string GetRandomUserAgent();
    static std::string GetChrome();
    static std::string GetFirefox();
    static std::string GetSafari();
    
    // Get realistic user agent for specific OS
    static std::string GetForWindows10();
    static std::string GetForWindows11();
    
private:
    static const std::vector<std::string> CHROME_UAS;
    static const std::vector<std::string> FIREFOX_UAS;
};
```

```cpp
// src/network/UserAgentProvider.cpp
const std::vector<std::string> UserAgentProvider::CHROME_UAS = {
    // Real Chrome user agents (update quarterly)
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
    
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/129.0.0.0 Safari/537.36",
};

std::string UserAgentProvider::GetRandomUserAgent() {
    // Return random real UA from current list
    return CHROME_UAS[rand() % CHROME_UAS.size()];
}
```

### Apply to All Requests

```cpp
// In DownloadManager/CurlFtpClient/HttpClient
curl_easy_setopt(curl, CURLOPT_USERAGENT, 
    UserAgentProvider::GetRandomUserAgent().c_str());

// Add realistic headers
struct curl_slist* headers = nullptr;
headers = curl_slist_append(headers, "Accept: */*");
headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
headers = curl_slist_append(headers, "Accept-Encoding: gzip, deflate, br");
headers = curl_slist_append(headers, "DNT: 1");
headers = curl_slist_append(headers, "Connection: keep-alive");
headers = curl_slist_append(headers, "Upgrade-Insecure-Requests: 1");
headers = curl_slist_append(headers, "Sec-Fetch-Dest: document");
headers = curl_slist_append(headers, "Sec-Fetch-Mode: navigate");
headers = curl_slist_append(headers, "Sec-Fetch-Site: none");
headers = curl_slist_append(headers, "Cache-Control: max-age=0");

curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
```

---

## Part 3: Behavioral Evasion (Timing & Patterns)

### Problem
Bots make requests at machine-speed. Humans have delays.

### Solution: Add Realistic Delays

```cpp
// src/network/BehavioralEvader.h
#pragma once
#include <chrono>

class BehavioralEvader {
public:
    // Add random human-like delay before request (200-2000ms)
    static void AddHumanDelay();
    
    // Random delay between consecutive requests (1-5 seconds)
    static void AddInterRequestDelay();
    
    // Simulate thinking time before form submission (3-8 seconds)
    static void AddFormSubmissionDelay();
    
    // Simulate page reading time
    static void SimulatePageReadTime(size_t pageSize);
    
private:
    static int GetRandomDelay(int minMs, int maxMs);
};
```

```cpp
// src/network/BehavioralEvader.cpp
#include "BehavioralEvader.h"
#include <thread>
#include <random>

int BehavioralEvader::GetRandomDelay(int minMs, int maxMs) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(minMs, maxMs);
    return dis(gen);
}

void BehavioralEvader::AddHumanDelay() {
    int delay = GetRandomDelay(200, 2000);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(delay)
    );
}

void BehavioralEvader::SimulatePageReadTime(size_t pageSize) {
    // Estimate reading time: ~200-300 words per minute
    // Average word length: 5 chars, plus 1 space = 6 chars per word
    int wordCount = pageSize / 6;
    int wordsPerMinute = 200 + (rand() % 100);  // 200-300 WPM
    int timeSeconds = (wordCount / wordsPerMinute) * 60;
    
    // Add variance: user might skim vs. read carefully
    int variance = GetRandomDelay(-3000, 3000);  // ±3 seconds
    
    std::this_thread::sleep_for(
        std::chrono::milliseconds(timeSeconds * 1000 + variance)
    );
}
```

### Use in Download Flow
```cpp
// src/application/GameIndexerService.cpp
void GameIndexerService::ScanStorageWithEvasion(const StoragePath& path) {
    for (const auto& game : games) {
        // Before requesting game metadata
        BehavioralEvader::AddHumanDelay();
        
        // Make request
        auto metadata = FetchGameMetadata(game);
        
        // Between requests
        BehavioralEvader::AddInterRequestDelay();
    }
}
```

---

## Part 4: Cookie Management (Cloudflare Bypass)

### Problem
Cloudflare Turnstile requires valid cookies to bypass.

### Solution: Cookie Jar with Persistence

```cpp
// src/network/CookieManager.h
#pragma once
#include <string>
#include <map>
#include <vector>

class CookieManager {
public:
    static void LoadCookies(const std::string& filePath);
    static void SaveCookies(const std::string& filePath);
    
    static void AddCookie(
        const std::string& name,
        const std::string& value,
        const std::string& domain = "",
        const std::string& path = "/",
        bool secure = true
    );
    
    static std::string GetCookieHeader(const std::string& domain);
    
    // Auto-refresh cookies if expired
    static void RefreshExpiredCookies();
    
private:
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path;
        std::time_t expires;
        bool secure;
    };
    
    static std::map<std::string, std::vector<Cookie>> cookieJar_;
};
```

```cpp
// src/network/CookieManager.cpp
#include "CookieManager.h"
#include <json/json.h>
#include <fstream>

void CookieManager::LoadCookies(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return;
    
    Json::Value cookies;
    file >> cookies;
    
    for (const auto& cookie : cookies) {
        AddCookie(
            cookie["name"].asString(),
            cookie["value"].asString(),
            cookie["domain"].asString(),
            cookie["path"].asString()
        );
    }
}

void CookieManager::SaveCookies(const std::string& filePath) {
    Json::Value output(Json::arrayValue);
    
    for (const auto& [domain, cookies] : cookieJar_) {
        for (const auto& cookie : cookies) {
            Json::Value c;
            c["name"] = cookie.name;
            c["value"] = cookie.value;
            c["domain"] = cookie.domain;
            c["path"] = cookie.path;
            c["expires"] = (Json::Value::Int64)cookie.expires;
            output.append(c);
        }
    }
    
    std::ofstream file(filePath);
    file << output;
}

std::string CookieManager::GetCookieHeader(const std::string& domain) {
    std::string header = "";
    
    if (cookieJar_.find(domain) != cookieJar_.end()) {
        for (const auto& cookie : cookieJar_[domain]) {
            if (header.length() > 0) header += "; ";
            header += cookie.name + "=" + cookie.value;
        }
    }
    
    return header;
}
```

### Use in Requests
```cpp
// src/network/CurlClient.cpp
void CurlClient::PerformRequest(const std::string& url) {
    CURL* curl = curl_easy_init();
    
    // Set user agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, 
        UserAgentProvider::GetRandomUserAgent().c_str());
    
    // Set cookies (if any exist for this domain)
    std::string domain = ExtractDomain(url);
    std::string cookies = CookieManager::GetCookieHeader(domain);
    if (!cookies.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookies.c_str());
    }
    
    // Make request
    curl_easy_perform(curl);
    
    // Save new cookies from response
    CookieManager::SaveCookies("data/cookies.json");
}
```

---

## Part 5: Proxy Support (Optional)

### For Advanced Evasion

```cpp
// src/network/ProxyManager.h
class ProxyManager {
public:
    static void SetProxy(const std::string& proxyUrl);
    // Format: "http://user:pass@host:port"
    
    static std::string GetNextProxy();  // Rotation
    
private:
    static std::vector<std::string> proxies_;
    static size_t currentIndex_;
};

// Usage in curl
curl_easy_setopt(curl, CURLOPT_PROXY, 
    ProxyManager::GetNextProxy().c_str());
```

---

## Part 6: Request Fingerprinting Prevention

### Make Requests Appear Unique

```cpp
// src/network/FingerprintMasker.h
class FingerprintMasker {
public:
    // Add time zone header matching system
    static std::string GetTimezoneHeader();
    
    // Add language header matching system locale
    static std::string GetLanguageHeader();
    
    // Add realistic referer
    static std::string GetReferer(const std::string& currentUrl);
    
    // Randomize request order/timing
    static void RandomizeRequestOrder(std::vector<Request>& requests);
};
```

---

## Implementation Order (Priority)

### 🔴 CRITICAL (Do First)
1. CapSkip integration - handles CAPTCHA (most common blocker)
2. User agent spoofing - prevents immediate detection
3. Realistic headers - passes basic WAF checks

### 🟠 IMPORTANT (Do Second)
4. Behavioral delays - evades timing-based detection
5. Cookie management - handles Cloudflare

### 🟡 NICE TO HAVE (Do Third)
6. Fingerprint masking - prevents advanced profiling
7. Proxy support - geographic spoofing

---

## Testing Checklist

- [ ] Can GTH pass reCAPTCHA v2 challenges?
- [ ] Can GTH bypass Cloudflare Turnstile?
- [ ] Do requests appear human-like in timing?
- [ ] Do user agents vary between requests?
- [ ] Are cookies properly persisted/reused?
- [ ] Does GTH work with proxy (if implemented)?
- [ ] Performance impact < 5%?

---

## Files to Create/Modify

```
NEW FILES:
src/captcha/CapSkipClient.h
src/captcha/CapSkipClient.cpp
src/network/UserAgentProvider.h
src/network/UserAgentProvider.cpp
src/network/BehavioralEvader.h
src/network/BehavioralEvader.cpp
src/network/CookieManager.h
src/network/CookieManager.cpp
src/network/ProxyManager.h
src/network/ProxyManager.cpp
src/network/FingerprintMasker.h

MODIFY:
src/network/CMakeLists.txt                      (add new sources)
src/application/DownloadManager.cpp             (use CapSkip, UserAgent)
src/application/GameIndexerService.cpp          (add delays)
src/ftp/CurlFtpClient.cpp                      (add headers/cookies)
src/network/LocalGameServer.cpp                 (if serves content)
```

---

## Dependencies to Add

In `CMakeLists.txt`:
- ✅ CURL (already included)
- ✅ nlohmann/json or jsoncpp (for cookie JSON)
- `winhttp.h` (Windows HTTP for cookie extraction)

---

## Example: Complete Request with Evasion

```cpp
void GameIndexerService::DownloadGameMetadataWithEvasion(
    const std::string& gameId,
    const std::string& gameUrl) {
    
    // Step 1: Add human delay
    BehavioralEvader::AddHumanDelay();
    
    // Step 2: Make request with spoofed headers
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, gameUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, 
        UserAgentProvider::GetRandomUserAgent().c_str());
    
    // Step 3: Add cookies
    std::string cookies = CookieManager::GetCookieHeader("api.example.com");
    if (!cookies.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookies.c_str());
    }
    
    // Step 4: Add realistic headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, 
        "Accept-Language: en-US,en;q=0.9");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Step 5: Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Step 6: Handle CAPTCHA if needed
    if (IsCloudflareChallenge(response)) {
        CapSkipClient capskip;
        std::string token = capskip.SolveTurnstile(
            sitekey, gameUrl
        );
        // Retry with CAPTCHA token
    }
    
    // Step 7: Save cookies for future requests
    CookieManager::SaveCookies("data/cookies.json");
    
    // Step 8: Delay before next request
    BehavioralEvader::AddInterRequestDelay();
}
```

---

## Result
With these implementations, GTH will:
✅ Pass CAPTCHA challenges automatically (CapSkip)
✅ Appear as legitimate Chrome browser
✅ Make requests at human-like speeds
✅ Handle Cloudflare Turnstile bypass
✅ Maintain session state via cookies
✅ Evade basic anti-bot detection

