# START HERE - GTH Browser Automation Framework

Welcome! This is your entry point to the Playwright-based browser automation module for GTH.

## 📦 What You Have

A complete, production-ready framework for:
- ✅ Testing CAPTCHA integration
- ✅ Automating browser interactions
- ✅ Creating custom test scenarios
- ✅ Integrating with CI/CD pipelines
- ✅ Debugging web components

## 🚀 Quick Start (Choose Your Path)

### Path 1: I Want to Start Immediately (5 minutes)
→ Read: **[INSTALLATION_QUICK_START.md](INSTALLATION_QUICK_START.md)**

```powershell
npm install
Copy-Item .env.example .env
notepad .env  # Set TARGET_URL
npm run test:manual
```

### Path 2: I Need Full Setup Instructions
→ Read: **[SETUP.md](SETUP.md)**

Detailed installation, troubleshooting, advanced configuration, and CI/CD integration.

### Path 3: I Want to Understand Everything
→ Read: **[README.md](README.md)**

Complete feature overview, architecture, API reference, examples, and best practices.

### Path 4: I'm Debugging CAPTCHA Issues
→ Read: **[CAPTCHA_TESTING.md](CAPTCHA_TESTING.md)**

Manual mode, test mode, production testing, debugging strategies, and best practices.

### Path 5: I Need a Command Reference
→ Read: **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)**

Commands, configuration, API, common issues, and keyboard shortcuts.

### Path 6: I Want to Understand the Architecture
→ Read: **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)**

Component details, data flow, file-by-file breakdown, and extension points.

## 📊 What's Included

| Component | Purpose | Details |
|-----------|---------|---------|
| **7 Core Modules** | Framework code | Browser, pages, CAPTCHA detection, logging |
| **2 Test Suites** | Quality assurance | Unit tests, integration tests |
| **3 Example Scripts** | Test execution | Manual, automated, custom sequences |
| **7 Documentation Files** | Guidance | Setup, usage, reference, troubleshooting |
| **45 Configuration Options** | Customization | Browser, timing, logging, debug |

## 📁 Directory Overview

```
browser-automation/
├── src/                    ← Framework code (7 modules)
├── tests/                  ← Tests (2 test suites)
├── scripts/                ← Test scripts (3 examples)
├── logs/                   ← Test logs (auto-created)
├── screenshots/            ← Screenshots (auto-created)
├── .env.example           ← Configuration template
├── package.json           ← Dependencies
└── Documentation/         ← 7 detailed guides
    ├── README.md
    ├── SETUP.md
    ├── INSTALLATION_QUICK_START.md
    ├── CAPTCHA_TESTING.md
    ├── QUICK_REFERENCE.md
    ├── PROJECT_STRUCTURE.md
    └── IMPLEMENTATION_SUMMARY.md
```

## 🎯 Common Tasks

| Task | Read This |
|------|-----------|
| **Get started now** | INSTALLATION_QUICK_START.md |
| **Install properly** | SETUP.md |
| **Run a test** | QUICK_REFERENCE.md (commands) |
| **Debug CAPTCHA** | CAPTCHA_TESTING.md |
| **Understand API** | README.md (usage examples) |
| **Configure settings** | QUICK_REFERENCE.md (environment variables) |
| **Understand architecture** | PROJECT_STRUCTURE.md |
| **Troubleshoot issues** | SETUP.md (troubleshooting) |
| **Check what's included** | IMPLEMENTATION_SUMMARY.md |

## ⚡ Fastest Path to Success

**5 minutes to your first test:**

```powershell
# 1. Install
npm install

# 2. Configure (minimal)
Copy-Item .env.example .env
# Edit .env: Set TARGET_URL to your test page

# 3. Run test
npm run test:manual

# 4. See results
type .\logs\*.log
ii .\screenshots\
```

## 🎓 Reading Order (Recommended)

For optimal understanding, read in this order:

1. **This file** (you are here)
2. **INSTALLATION_QUICK_START.md** - Get it running
3. **README.md** - Understand features
4. **QUICK_REFERENCE.md** - Know the commands
5. **CAPTCHA_TESTING.md** - Master CAPTCHA testing
6. **PROJECT_STRUCTURE.md** - Deep dive into architecture

## 📞 Help & Support

### If you're stuck:

1. **Check logs:** `./logs/automation-*.log`
2. **View screenshots:** `./screenshots/`
3. **Enable debug mode:** Set `DEBUG=true` in `.env`
4. **Read troubleshooting:** See SETUP.md
5. **Check configuration:** Edit `.env` based on QUICK_REFERENCE.md

### Common Issues & Solutions:

| Issue | Solution |
|-------|----------|
| Browser won't open | Set `HEADLESS=false` in `.env`, check logs |
| CAPTCHA not detected | Add `data-captcha` to CAPTCHA element |
| Test hangs | Increase timeout: `CAPTCHA_COMPLETION_TIMEOUT=600000` |
| Chrome not found | Install Chrome or set `BROWSER_EXECUTABLE_PATH` |
| Can't connect to page | Check `TARGET_URL` in `.env` |

## 🔑 Key Concepts

### Browser Automation
Real Chromium browser controlled via Playwright. No headless-only limitations.

### CAPTCHA Testing
Two modes:
- **Manual:** Browser visible, you solve CAPTCHA manually
- **Automated:** Uses test signals from your implementation

### Sessions
Browser session persists across page navigations. Useful for testing login flows.

### Logging
Every action logged with timestamps. Screenshots saved automatically on error.

### Security
Only allowed hosts can be tested. No CAPTCHA bypass logic. Manual interaction required for real CAPTCHAs.

## 📦 System Requirements

- Node.js 18.0.0 or higher
- npm 9.0.0 or higher
- 2GB RAM minimum
- 1GB disk space for logs/screenshots
- Windows 10/11 Pro

## 🚀 Commands Quick Reference

```powershell
# Install
npm install

# Run tests
npm run test:manual          # Interactive test (browser visible)
npm run test:captcha         # Automated workflow test
npm run test                 # Unit tests
npm run test:integration     # Integration tests

# Debugging
$env:DEBUG="true"
$env:KEEP_BROWSER_OPEN="true"
npm run test:manual
```

## 🎯 What Each Component Does

| Component | Role |
|-----------|------|
| **BrowserManager** | Launches/closes browser, creates pages |
| **PageController** | Navigates pages, clicks, types, screenshots |
| **CaptchaMonitor** | Detects CAPTCHA, tracks state, waits for completion |
| **SessionManager** | Orchestrates tests, runs interaction sequences |
| **AutomationLogger** | Logs events, saves screenshots, tracks sessions |
| **Configuration** | Manages settings from environment variables |

## 📝 File Quick Reference

| File | When to Read |
|------|--------------|
| START_HERE.md | First thing (you are here!) |
| INSTALLATION_QUICK_START.md | Getting started |
| README.md | Understanding features |
| SETUP.md | Detailed installation & troubleshooting |
| QUICK_REFERENCE.md | Finding commands & configuration |
| CAPTCHA_TESTING.md | CAPTCHA-specific issues |
| PROJECT_STRUCTURE.md | Understanding architecture |
| IMPLEMENTATION_SUMMARY.md | What's included |

## ✅ Pre-Launch Checklist

Before running your first test:

- [ ] Node.js 18+ installed
- [ ] npm installed
- [ ] You're in the `browser-automation` directory
- [ ] Test server is running
- [ ] You know your test page URL

## 🎯 Next Step

**Choose one:**

1. **Fast start:** Read INSTALLATION_QUICK_START.md
2. **Full setup:** Read SETUP.md
3. **Just run it:** 
   ```powershell
   npm install
   Copy-Item .env.example .env
   npm run test:manual
   ```

## 💡 Pro Tips

1. **Keep notes:** Document your configuration in `.env`
2. **Enable debug:** `DEBUG=true` helps troubleshooting
3. **Check logs:** Always look at `./logs/` when debugging
4. **Take screenshots:** Auto-saved in `./screenshots/`
5. **Test incrementally:** Start simple, add complexity
6. **Read docs:** Answers are in the documentation

## 🚀 You're Ready!

Everything you need is here. Pick your path above and get started!

**Questions?** Check the appropriate documentation file listed above.

---

**Start here:** [INSTALLATION_QUICK_START.md](INSTALLATION_QUICK_START.md) (5 minutes)

or

**Learn everything:** [README.md](README.md) (comprehensive guide)
