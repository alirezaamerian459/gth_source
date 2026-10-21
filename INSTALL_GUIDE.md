# Game Transfer Hub v2.0.0 - Installation Guide

## 📦 Installation Options

Choose the method that works best for you:

### Quick Installation Matrix

| Method | Time | Complexity | Best For |
|--------|------|-----------|----------|
| **Installer** | 5 min | ⭐ Easy | Most users |
| **Portable** | 2 min | ⭐ Very Easy | USB/portable use |
| **Build from Source** | 15 min | ⭐⭐⭐ Advanced | Developers |

---

## Option 1: Installer (Recommended) ⭐

### Requirements
- Windows 10/11 64-bit
- Administrator privileges
- 200 MB free disk space
- ~5 minutes

### Steps

1. **Download** 
   ```
   GameTransferHub-Setup-Latest.exe
   ```

2. **Run Installer**
   - Double-click the executable
   - Click "Yes" when prompted for admin rights
   - Accept license agreement
   - Choose installation folder (default: C:\Program Files\GameTransferHub)

3. **Automatic Setup**
   - Dependencies installed automatically
   - Desktop shortcut created
   - Start menu shortcuts added
   - Windows registry updated

4. **Launch App**
   - Double-click desktop shortcut, OR
   - Start menu → Game Transfer Hub → Game Transfer Hub

5. **Add Storage**
   - Settings → Storage → Add Storage
   - Select folder with your .pkg files

6. **Scan Games**
   - Games tab → "Scan All Storages"
   - Your games appear! ✅

### Uninstall
- Control Panel → Programs → Uninstall a Program
- Find "Game Transfer Hub"
- Click Uninstall
- All files removed cleanly

---

## Option 2: Portable Version ⭐⭐

### Requirements
- Windows 10/11 64-bit
- 200 MB free disk space
- ~2 minutes
- Can run from USB drive

### Steps

1. **Download**
   ```
   GameTransferHub-Portable.zip
   ```

2. **Extract**
   - Right-click → Extract All
   - Choose destination folder
   - Can be anywhere: Desktop, USB drive, Documents, etc.

3. **Run**
   - Navigate to extracted folder
   - Double-click `GameTransferHubApp.exe`
   - No installation needed!

4. **Configuration**
   - Settings → Storage → Add Storage
   - Select folder with .pkg files

5. **Scan**
   - Games tab → "Scan All Storages"
   - Done! ✅

### Advantages
- ✅ No installation needed
- ✅ Works from USB drive
- ✅ Can run without admin rights
- ✅ Easy to move/copy
- ✅ No registry modifications

### Note
- First run creates config files in %APPDATA%\GameTransferHub
- Database stored in %APPDATA%\GameTransferHub\gth.db
- Logs in %APPDATA%\GameTransferHub\logs\

---

## Option 3: Build from Source ⭐⭐⭐

### Requirements
- Visual Studio 2022 (Community Edition FREE)
- ATL/WTL components installed
- CMake 3.15+
- 2GB free disk space
- ~15-20 minutes

### Prerequisites

**Install Visual Studio 2022:**
1. Download from: https://visualstudio.microsoft.com/vs/
2. Choose: Desktop Development with C++
3. During installation, add:
   - Individual Components tab
   - Search "ATL"
   - Check: C++ ATL for latest v143 build tools

**Install CMake (if needed):**
```bash
1. Download from: https://cmake.org/download/
2. Run installer
3. Add to PATH when asked
```

### Build Steps

1. **Open Command Prompt**
   ```bash
   cd C:\Users\YourUsername\Desktop\local
   ```

2. **Build**
   ```bash
   build.bat
   ```
   Or manually:
   ```bash
   cmake --build build --config Release
   ```

3. **Wait for Completion**
   - First build: 10-15 minutes
   - Later builds: 2-5 minutes
   - Do NOT close the window

4. **Verify Success**
   - Check for: `BUILD SUCCESS!` message
   - Executable at: `build\Release\GameTransferHubApp.exe`

5. **Run**
   - Double-click the executable
   - Or copy to desired location

### Troubleshooting Build

**Error: "Cannot find atlbase.h"**
→ Install ATL via Visual Studio Installer

**Error: "cmake not found"**
→ Install CMake or add to PATH

**Error: "MSBuild failed"**
→ Make sure Visual Studio is installed completely
→ Try: File → Source Control → Git → Resync

**Build taking too long?**
→ Normal! First build takes time
→ Grab coffee ☕

---

## Post-Installation Setup

### For All Installation Methods

#### 1. Add Storage Location

```
App → Settings → Storage
Click: "Add Storage"
Select: Folder containing your .pkg files
Click: "Add"
```

You can add multiple storage locations!

#### 2. Create Directory Structure (Optional)

Organize your .pkg files (optional - app finds them anywhere):

```
D:\Games\
├── game1.pkg
├── game2.pkg
└── by-region\
    ├── usa-games\game3.pkg
    └── eur-games\game4.pkg
```

#### 3. First Scan

```
Games Tab → Click: "Scan All Storages"
Wait for completion (status shows progress)
Your games appear in the list!
```

#### 4. Check Updates

```
Updates Tab → Click: "Check for Updates"
Available updates are shown
Select game → Click: "Queue Selected" to download
```

---

## Verification Checklist

After installation, verify:

- [ ] App launches without errors
- [ ] Settings → Storage works
- [ ] Can add storage paths
- [ ] Games tab is visible
- [ ] Updates tab is visible
- [ ] Can click "Scan All Storages"
- [ ] At least one .pkg file is recognized

---

## Database Location

Your games database is stored at:

**Windows:**
```
%LOCALAPPDATA%\GameTransferHub\gth.db
```

**Full path:**
```
C:\Users\YourUsername\AppData\Local\GameTransferHub\gth.db
```

### Backup Database

```bash
1. Navigate to: C:\Users\YourUsername\AppData\Local\GameTransferHub\
2. Copy: gth.db
3. Paste to safe location
```

Restore: Copy gth.db back to same location

---

## Logs & Troubleshooting

### View Logs

**Location:**
```
%LOCALAPPDATA%\GameTransferHub\logs\
```

**Latest log file:**
- Named by date: `gth_2026-09-16.log`

### Common Issues

| Problem | Solution |
|---------|----------|
| App won't start | Run as administrator, check Windows Defender |
| Games not found | Verify storage path added, check .pkg files exist |
| Slow performance | Use local drive instead of network storage |
| Database error | Delete gth.db and rescan (starts fresh) |
| Missing DLLs | Reinstall using installer (fixes dependencies) |

### Reset Application

If you need to start fresh:

```bash
1. Stop the app
2. Delete: %LOCALAPPDATA%\GameTransferHub\gth.db
3. Keep: settings.ini (preserves your settings)
4. Restart app
5. Rescan games
```

---

## System Paths Reference

### Important Folders

| Purpose | Windows Path |
|---------|-------------|
| App data | %LOCALAPPDATA%\GameTransferHub |
| Database | %LOCALAPPDATA%\GameTransferHub\gth.db |
| Logs | %LOCALAPPDATA%\GameTransferHub\logs |
| Settings | %LOCALAPPDATA%\GameTransferHub\settings.ini |
| Install (if using installer) | %ProgramFiles%\GameTransferHub |

### Quick Access

Open in File Explorer:
```
Windows + R
Type: %LOCALAPPDATA%\GameTransferHub
Press: Enter
```

---

## Next Steps

### Getting Started

1. ✅ Install the app (using option above)
2. ✅ Add storage location
3. ✅ Copy .pkg files to that location
4. ✅ Scan games
5. ✅ Check for updates
6. ✅ Enjoy! 🎮

### Organize Your Games

See: `GAME_RECOGNITION_GUIDE.md` for organization tips

### Learn Features

See: `QUICK_REFERENCE.md` for quick facts

---

## Need Help?

### Quick Reference
- `QUICK_REFERENCE.md` - Common questions
- `GAME_RECOGNITION_GUIDE.md` - How to use
- `RELEASE_NOTES.md` - What's new

### Detailed Information
- `PKG_SUPPORT.md` - Technical details
- `IMPLEMENTATION_SUMMARY.md` - Implementation info

---

## Support

| Issue | Action |
|-------|--------|
| Installation fails | Run installer with admin rights |
| App crashes | Check logs in logs/ folder |
| Games not found | Verify storage path, rescan |
| Performance issues | Use SSD instead of HDD |
| Uninstall | Control Panel → Programs → Uninstall |

---

**You're all set!** 🚀

Choose your installation method above and get started!

Happy gaming! 🎮
