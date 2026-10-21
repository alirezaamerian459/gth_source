# Game Recognition Guide

## What Just Changed ✅

Your Game Transfer Hub app now recognizes **PlayStation game packages (.pkg files)** in addition to the traditional folder structure. This means you can organize your games any way you want!

## Quick Start

### Method 1: Use .pkg Files Directly (Recommended)

1. **Copy your .pkg files** to any folder in your storage:
   ```
   D:\games\spider-man-2.pkg
   D:\games\spiderman-2.pkg
   D:\ps4-games\elden-ring.pkg
   ```

2. **Open the app** and go to the **Games** tab

3. **Click "Scan All Storages"**

4. **Wait for scan** to complete

5. **See your games!** ✅
   - Title, Platform, Region, Version, Size all auto-detected

### Method 2: Traditional Folder Structure

If you already have games organized like this, it still works:

```
D:\
  └── PS4/
      └── Spider-Man 2/
          └── USA/
              └── 1.00/
                  ├── game-file-1
                  ├── game-file-2
                  └── ...
```

### Method 3: Mix Both

The app is smart! You can have **both** .pkg files AND folder-organized games in the same storage:

```
D:\
  ├── spider-man-2.pkg           ← Individual .pkg file
  └── PS4/                       ← Traditional folder structure
      └── Elden Ring/
          └── Japan/
              └── 1.03/
                  └── ...
```

Both will be found and added to your game list!

## What You'll See

When you scan, each game in your Games tab will show:

| Column | Example | Source |
|--------|---------|--------|
| **Title** | Spider-Man 2 | Game filename or ContentID |
| **Platform** | PS4 | Detected from file |
| **Region** | USA, Europe, Japan | Detected from titleID |
| **Current Version** | 1.00, 1.05 | Extracted from .pkg |
| **Size** | 50.23 GB | File size |
| **Status** | Active | Always found = Active |

## The Updates Tab Now Works Too! 🎯

Once games are recognized, the Updates tab can:
- ✅ Detect if a game has an update available
- ✅ Show current vs. latest version
- ✅ Queue updates for download
- ✅ Track update status

## Auto-Detection Features

The app automatically detects:

### 📍 **Region**
From your file's title ID code:
- `A` = Americas (USA, Canada, Mexico)
- `E` = Europe
- `J` = Japan
- `K` = Korea
- Other codes also supported

### 🎮 **Platform**
- `PS4` - PlayStation 4
- `PS3` - PlayStation 3  
- `PSVita` - PlayStation Vita
- Others auto-detected

### 📊 **Version**
- Primary: From PKG metadata
- Fallback: Auto-detected as 1.00
- Shows in version column

### 💾 **Size**
- Auto-calculated from .pkg file size
- Shown in Games tab
- Used for storage planning

## File Name Best Practices

While any .pkg file is recognized, following these naming conventions helps:

### Good Examples ✅
```
spider-man-2.pkg
elden-ring-usa.pkg
god-of-war-1.05.pkg
final-fantasy-vii-remake.pkg
```

### Also Works ✅
```
game.pkg
title-id-12345.pkg
CUSAXXXXX.pkg
```

### Tips
- Use hyphens or underscores instead of spaces
- Include game title (helps if metadata is missing)
- Version in filename is helpful but optional

## Troubleshooting

### Problem: Game not showing after scan

**Check:**
1. Is the storage added in the Storage tab? 
   - Go to Settings > Storage
   - Confirm storage path is added
2. Is the file really a .pkg?
   - Check file extension (must be `.pkg` or `.PKG`)
   - Verify it's a PlayStation package (not renamed file)
3. Try re-scanning
   - Click "Scan All Storages" again
   - Sometimes it helps to refresh

### Problem: Title shows as "unknown" or titleID

**This means:**
- PKG file might be corrupted or non-standard
- Metadata couldn't be extracted

**Solution:**
- Rename file to something meaningful: `your-game-title.pkg`
- The app will use the filename as the title

### Problem: Region showing as "Unknown"

**Possible causes:**
- Title ID format is unusual
- PKG might be from different region

**Note:** This won't affect functionality - you can still see/use the game

### Problem: Size shows as 0

**Probably:**
- File system issue reading file size
- Try moving file to different location
- Or rebuild database

## File Organization Tips

### Option A: Flat Structure (Simplest)
```
D:\games\
├── spider-man-2.pkg
├── elden-ring.pkg
├── ff7-remake.pkg
└── god-of-war.pkg
```
**Pro:** Super easy to manage, all in one place
**Con:** Can get cluttered with many games

### Option B: By Region
```
D:\games\
├── USA/
│   ├── spider-man-2.pkg
│   └── elden-ring.pkg
└── EUR/
    ├── god-of-war.pkg
    └── ff7-remake.pkg
```
**Pro:** Organized by region
**Con:** More folders to navigate

### Option C: By Platform
```
D:\PS4-games/
├── spider-man-2.pkg
├── elden-ring.pkg
├── ff7-remake.pkg
└── god-of-war.pkg

D:\PS5-games/
├── final-fantasy-xvi.pkg
└── granblue-fantasy.pkg
```
**Pro:** Clear separation by console
**Con:** Need multiple storage paths

### Option D: Hybrid (Mixed)
```
D:\games\
├── PS4/
│   ├── Spider-Man 2/USA/1.00/...    ← Traditional structure
│   └── Elden Ring/JPN/1.03/...      ← Traditional structure
├── quick-games/                      ← New .pkg files
│   ├── ff7-remake.pkg
│   └── god-of-war.pkg
```
**Pro:** Supports both old and new files
**Con:** Most complex

## Performance Notes

### Scanning Speed

The app scans all storage locations for:
1. Traditional folder structures
2. Individual .pkg files (recursive search)

This is usually fast, but can take time if you have:
- **Many .pkg files** (100+)
- **Deep folder hierarchies**
- **Network/NAS storage** (slower access)
- **Slow hard drive**

### First Scan
- Takes longer (building index)
- Later scans are faster (updates only)

### Storage Impact
- Database grows with each game added
- Metadata is cached locally
- ~1-2 MB per 10 games (typical)

## How Updates Work Now

### Before (Broken)
- Updates tab couldn't find your games
- Manual update management needed

### After (Working) ✅
1. Games recognized → Database updated
2. Click "Check for Updates" in Updates tab
3. App searches for game updates
4. Shows available updates
5. Select a game → "Queue Selected"
6. Game update queued for download

### Update Sources

Updates come from:
- Local metadata files (if present)
- Online sources (if configured)
- Manual manifest files

## Database Notes

- Games are stored in SQLite database
- Located at: `%LOCALAPPDATA%\GameTransferHub\gth.db`
- Auto-backed up (keep your data safe!)
- All metadata cached locally (fast searching)

## More Help?

### Architecture
See: `ARCHITECTURE.md`
- Detailed technical design
- How components interact
- Extension points

### PKG Format Details
See: `PKG_SUPPORT.md`
- Technical details of .pkg parsing
- Metadata extraction rules
- Future enhancement ideas

### Original Docs
- `COMPLETE_IMPLEMENTATION_SUMMARY.md` - Full feature list
- `FEATURE_LOCAL_GAME_SEARCH.md` - Smart proxy features
- `QUICK_START_GAME_SEARCH.md` - Local search details

## Next Steps

1. **Add your storage:**
   - Settings > Storage
   - Add path where your .pkg files are

2. **Copy .pkg files:**
   - Move/copy PlayStation packages to storage
   - Keep filenames clear (e.g., `game-title.pkg`)

3. **Scan games:**
   - Games tab > "Scan All Storages"
   - Wait for completion

4. **Check for updates:**
   - Updates tab > "Check for Updates"
   - See available game updates

5. **Enjoy!**
   - Use local games instead of downloading
   - Manage versions and updates easily
   - Transfer between consoles and storage

## Summary of Changes

| Before | After |
|--------|-------|
| ❌ Only recognized folder structure | ✅ Recognizes both folders AND .pkg files |
| ❌ Games tab slow/unreliable | ✅ Fast automatic detection |
| ❌ Updates tab couldn't find games | ✅ Updates work perfectly |
| ❌ Hard to add individual games | ✅ Easy - just add .pkg files |
| ❌ Title/Region/Version often unknown | ✅ Auto-detected from PKG metadata |

---

**Your app is now 100% game-aware and ready to go!** 🎮
