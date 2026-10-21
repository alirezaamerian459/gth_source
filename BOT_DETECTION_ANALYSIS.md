# Bot Detection Analysis Report
**GTH v5.2 - Robot Detector Enhancement**

---

## Executive Summary

You have 2 bot/evasion tools:
1. **CapSkip** - CAPTCHA solver (local HTTP API, port 8080)
2. **Appointment Booking Bot** - Selenium-based web automation with proxy rotation

Both use specific evasion techniques. Below is how to detect them and proposed bot detection mechanisms for GTH.

---

## BOT #1: CapSkip (CAPTCHA Solver)

### What It Is
- Local HTTP server running on `127.0.0.1:8080`
- Solves: reCAPTCHA v2/v3, Cloudflare Turnstile, GeeTest, ALTCHA, Image CAPTCHAs
- Uses AI-powered solving on local machine
- Exposes standard HTTP endpoints: `in.php`, `res.php`

### How It Works
```
Script calls → http://127.0.0.1:8080/in.php
↓
CapSkip API receives CAPTCHA
↓
Local AI solver processes it
↓
Script polls → http://127.0.0.1:8080/res.php
↓
Gets solution token (g-recaptcha-response, etc.)
↓
Script submits form with token
```

### Evasion Techniques
1. **Local Processing** - CAPTCHA never leaves the machine
2. **Standard HTTP API** - Uses familiar `in.php`/`res.php` endpoints (hard to block)
3. **No Rate Limiting** - Flat subscription = unlimited solves
4. **SDK Integration** - Python, Node.js, .NET, PHP libraries
5. **Browser Integration** - Chrome/Firefox extensions

### **Detection Method for CapSkip**
```cpp
// Check for CapSkip process or port 8080 listening
bool IsCapSkipRunning() {
    // 1. Check if process "CapSkip.exe" is running
    // 2. Check if localhost:8080 is listening
    // 3. Check for CapSkip registry entries (HKLM/HKCU)
    // 4. Monitor HTTP requests to 127.0.0.1:8080
}
```

**Indicators:**
- Process: `CapSkip.exe` or similar
- Network: TCP port 8080 localhost
- Registry: `HKCU\Software\CapSkip` or `HKLM\SOFTWARE\CapSkip`
- HTTP endpoints: `/in.php`, `/res.php` to `127.0.0.1:8080`

---

## BOT #2: Appointment Booking Bot (Selenium Automation)

### What It Is
- Python Selenium WebDriver bot
- Targets: `schedule.cf-grcon-isl-pakistan.com` appointment booking system
- Features: Proxy rotation, cookie injection, form auto-fill, screenshot capture
- Uses: `undetected_chromedriver` (UC) library

### How It Works
```
Main Loop:
  Load clients from JSON
  ↓
  For each client:
    Launch undetected Chrome
    ↓
    Inject cookies (bypass Cloudflare Turnstile)
    ↓
    Auto-login (if needed)
    ↓
    Click available appointment slot
    ↓
    Fill form with client data
    ↓
    Submit form
    ↓
    Save booking to CSV
```

### Evasion Techniques

#### 1. **Undetected ChromeDriver (UC)**
- Removes Selenium detection flags
- Patches WebDriver properties
- Disables automation indicators
```python
chrome_options.add_argument("--disable-blink-features=AutomationControlled")
driver = uc.Chrome(version_main=141)  # Matches real Chrome version
```

#### 2. **Cookie Injection**
```python
# Bypass Cloudflare Turnstile by injecting valid cookies
driver.get(base_url)
driver.add_cookie({
    "name": "cf_clearance",
    "value": "valid_token_from_previous_session",
    ...
})
driver.refresh()
```

#### 3. **Proxy Rotation**
```python
# Create Chrome extension for proxy authentication
proxy_url = "http://user:pass@host:port"
# Extension handles proxy auth transparently
```

#### 4. **Headless Evasion**
```python
chrome_options.add_argument("--disable-infobars")
chrome_options.add_argument("--disable-extensions")
chrome_options.add_argument("--no-sandbox")
chrome_options.add_argument("--disable-dev-shm-usage")
chrome_options.add_argument("--start-maximized")
chrome_options.add_argument("--headless=new")  # Modern headless (harder to detect)
```

#### 5. **Anti-Analysis**
```python
chrome_options.add_argument('--ignore-certificate-errors')
chrome_options.add_argument('--allow-insecure-localhost')
# Disables security warnings that indicate automation
```

### **Detection Methods for Appointment Bot**

#### **Method 1: Process Detection**
```cpp
bool IsSeleniumRunning() {
    // Check for:
    // - "chromedriver.exe"
    // - "undetected_chromedriver" processes
    // - "python.exe" with Selenium import
    // - Chrome with --disable-blink-features flag
}
```

**Indicators:**
- Process: `chromedriver.exe`, `python.exe` (running bot script)
- Command line args: `--disable-blink-features=AutomationControlled`
- Chrome launched with `--headless=new`

#### **Method 2: File System Detection**
```cpp
bool HasBotArtifacts() {
    // Check for:
    // - cookies.json (in appointment bot root)
    // - data/clients3.json (client list)
    // - data/login.json (credentials)
    // - data/bookings.csv (bot output)
    // - Python Selenium packages in site-packages
}
```

#### **Method 3: Network Behavior Detection**
```cpp
bool DetectAnomalousNetworkActivity() {
    // Monitor for:
    // 1. Rapid sequential HTTP requests (non-human pace)
    // 2. Requests to known proxy services
    // 3. User agents that don't match OS/installed browser
    // 4. Missing/unusual HTTP headers (User-Agent, Accept-Language, etc.)
}
```

**Bot Signatures:**
- Requests to proxy services (proxy.com, proxy-api.com, etc.)
- Consistent timing (e.g., every 5 seconds exactly)
- Unusual User-Agent strings
- Missing `Accept-Language` header
- `python-requests` or `urllib` User-Agent

#### **Method 4: Behavioral Analysis**
```cpp
bool DetectBotBehavior() {
    // Monitor for:
    // 1. Screenshot files (index.png, login_completed.png, done_booking.png)
    // 2. CSV files being written to repeatedly
    // 3. Form submission timing (too fast or too consistent)
    // 4. No mouse movement, only keyboard/programmatic clicks
    // 5. Perfect input filling (no typos, corrections, hesitations)
}
```

#### **Method 5: Registry/Environment Detection**
```cpp
bool DetectDevTools() {
    // Check for:
    // - Python installed (HKLM\SOFTWARE\Python)
    // - Selenium installed in site-packages
    // - undetected_chromedriver in Python packages
    // - Environment variable: PYTHONPATH
    // - Presence of VS Code with Python extension (dev environment)
}
```

---

## Detection Strategy for GTH

### **Tier 1: Quick Checks** (Run on startup)
1. Check if CapSkip.exe is running
2. Check if localhost:8080 is listening
3. Scan for known bot processes (chromedriver, python.exe running bot scripts)
4. Check for suspicious Python packages

### **Tier 2: Active Monitoring** (During operation)
1. Monitor HTTP requests to localhost ports (8080, 9999, etc.)
2. Track timing of file system operations (suspicious CSV writes, repeated access)
3. Monitor for rapid sequential API calls
4. Detect clipboard operations (bots often paste data)
5. Monitor Windows event logs for suspicious processes

### **Tier 3: Heuristic Analysis** (Ongoing)
1. User behavior analysis (click timings, movement patterns)
2. Form fill detection (perfect accuracy = likely bot)
3. Network pattern analysis (consistent intervals = bot)
4. Screenshot file creation (common bot tactic)

---

## Proposed GTH Bot Detector Module

### Architecture
```
BotDetectorService
├── ProcessDetector
│   ├── DetectCapSkip()
│   ├── DetectChromeDriver()
│   ├── DetectPythonBots()
│   └── MonitorSuspiciousProcesses()
├── FileSystemDetector
│   ├── DetectBotArtifacts()
│   ├── DetectSuspiciousFiles()
│   └── MonitorFileActivity()
├── NetworkDetector
│   ├── CheckLocalPortsForListeners()
│   ├── AnalyzeRequestPatterns()
│   └── DetectProxyUsage()
├── BehavioralDetector
│   ├── AnalyzeClickPatterns()
│   ├── AnalyzeFormFillingSpeed()
│   └── DetectClipboardUsage()
└── TelemetryCollector
    ├── LogDetectionEvents()
    ├── TrackBotIndicators()
    └── GenerateSecurityReport()
```

### Key Detectable Signatures

| Bot | Detection Method | Confidence |
|-----|------------------|------------|
| **CapSkip** | Process (CapSkip.exe) | Very High |
| **CapSkip** | Port 8080 listener | High |
| **CapSkip** | Registry key | High |
| **Appointment Bot** | Process (chromedriver.exe) | Very High |
| **Appointment Bot** | Command line flags (--headless) | High |
| **Appointment Bot** | File artifacts (cookies.json) | High |
| **Appointment Bot** | Network patterns (proxy usage) | Medium |
| **Appointment Bot** | Behavior (screenshot files) | Medium |

---

## Evasion Counter-Measures

### If Bot Uses Evasion
```
Bot Evasion Method          | GTH Counter-Measure
─────────────────────────────────────────────────────
"Hide" bot process          → Monitor CPU/Memory anomalies
Kill CapSkip before GTH use → Detect residual port 8080
Disable CapSkip                 listening, registry cleanup
Use alternate ports         → Scan common alternate ports
                              (8081, 9090, 9999)
Delete cookie artifacts     → Monitor in-memory usage patterns
                            → Detect pattern instead of file
Use VPN/Proxy              → Detect geo-location mismatches
                            → Monitor unusual IP patterns
```

---

## Implementation Recommendations

### Phase 1: Core Detection (Week 1)
- [ ] Process detection (CapSkip, ChromeDriver)
- [ ] Port monitoring (8080, 9090, etc.)
- [ ] Registry scanning
- [ ] File artifact detection

### Phase 2: Active Monitoring (Week 2)
- [ ] Network behavior analysis
- [ ] File system activity monitoring
- [ ] Timing pattern analysis
- [ ] Screenshot detection

### Phase 3: Heuristic Analysis (Week 3)
- [ ] Behavioral pattern recognition
- [ ] ML-based anomaly detection
- [ ] User interaction profiling
- [ ] Report generation

### Phase 4: Response Mechanisms (Week 4)
- [ ] Logging/alerting
- [ ] Graceful shutdown on detection
- [ ] Security event reporting
- [ ] Telemetry collection

---

## Testing Plan

### Test Vectors
1. **CapSkip Test**: Run CapSkip.exe, then launch GTH → Should detect
2. **Appointment Bot Test**: Run bot script while GTH is open → Should detect
3. **Combo Test**: Both running simultaneously → Should detect both
4. **Evasion Test**: Bot deletes artifacts, kills processes → Should detect via patterns

### Success Criteria
- ✅ Detects CapSkip within 2 seconds of startup
- ✅ Detects Appointment Bot within 5 seconds of form interaction
- ✅ 0% false positives with legitimate usage
- ✅ Performance impact < 2% CPU overhead

---

## Security Considerations

⚠️ **Important Notes:**
1. Detection can be evaded with enough sophistication
2. Heuristic-based detection may have false positives
3. Combine multiple detection methods for best accuracy
4. Monitor and update signatures regularly
5. Privacy: Log only security-relevant events

---

## Files to Modify in GTH

```
src/application/CMakeLists.txt
  ↓ Add new service

src/application/BotDetectorService.h
src/application/BotDetectorService.cpp
  ↓ Main detection service

src/core/BotIndicator.h
  ↓ Data structures

src/app/SettingsView.cpp
  ↓ UI for bot detection results

src/logging/Logging.cpp
  ↓ Log bot detection events
```

---

**Next Steps:**
1. Review this analysis
2. Decide which detection methods to implement
3. Start with Phase 1 (Core Detection)
4. Test against both bots
5. Iterate and refine

