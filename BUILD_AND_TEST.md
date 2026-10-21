# 🚀 Quick Build & Test Guide

## One-Command Build

```bash
cd C:\Users\DrGame\Desktop\local
rmdir /s /q build 2>nul
cmake -G "Visual Studio 17 2022" -A x64 -B build && cmake --build build --config Debug
```

## Run the App

```bash
build\src\app\Debug\GameTransferHub.exe
```

---

## ✅ Test Checklist

### Test 1: App Launches
- [ ] No crash on startup
- [ ] Services initialize
- [ ] Main window appears

### Test 2: Games Indexed
- [ ] Add a local storage via Dashboard
- [ ] Run storage scan
- [ ] Games appear in list

### Test 3: URL Auto-Search
- [ ] Set up HTTP proxy to capture URLs
- [ ] Console requests game download
- [ ] URL appears in CDN URL list
- [ ] Check "Source" column:
  - [ ] Shows "Local" if game found locally
  - [ ] Shows "Download" if not found
  - [ ] Mark shows "Local: [game name]" if found

### Test 4: Local Path Works
- [ ] Double-click local game entry
- [ ] Path copies to clipboard
- [ ] Path points to correct folder
- [ ] Can transfer via FTP using this path

---

## 📋 File Locations

| What | Where |
|------|-------|
| Main app | `build\src\app\Debug\GameTransferHub.exe` |
| Database | `%LOCALAPPDATA%\GameTransferHub\gth.db` |
| Logs | `%LOCALAPPDATA%\GameTransferHub\logs\` |
| Implementation | `src/application/{storage,ftp}/` |
| Tests | `build\tests\unit\` |

---

## 🔍 Verify Implementation

```bash
# Check files compiled
dir build\src\application\CMakeFiles\gth_application.dir\src | find "GameSearch"

# Check app uses feature
tasklist | find "GameTransferHub.exe"
```

---

## 📊 What You Should See

### CDN URL List (New Layout)
```
URL                          Mark                  Local Path              Source      Captured
https://psn-cdn.../spider... Local: Spider-Man 2   D:\Games\PS5\Spi...     Local       2024-09-15
https://psn-cdn.../new-g...  (empty)               (empty)                 Download    2024-09-15
```

---

## 🐛 If Something Fails

### Build Error: "Cannot find GameSearchService"
```bash
# Solution: Verify files exist
ls src/application/src/GameSearch*
```

### Build Error: "Unresolved external symbol"
```bash
# Check includes are correct
grep -r "IGameSearchService" src/application/
```

### Feature Doesn't Work
```bash
# Check games are indexed
# Dashboard → Storage → Run Scan

# Check database has entries
# Look at %LOCALAPPDATA%\GameTransferHub\logs\
```

---

## 🎯 Expected Results

**Before**: Every game needs download link  
**After**: Local games auto-detected, download as fallback

---

## 📞 Support

See documentation files:
- `DEPLOYMENT_SUMMARY.md` - Overview
- `FEATURE_LOCAL_GAME_SEARCH.md` - Technical details
- `QUICK_START_GAME_SEARCH.md` - Detailed guide
- `IMPLEMENTATION_CHECKLIST.md` - Verification steps

---

**Ready to build? Run the command above! 🚀**
