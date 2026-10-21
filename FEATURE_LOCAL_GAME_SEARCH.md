# Automatic Local Game Search Feature

## Overview
This feature adds automatic detection of games in local HDDs when a game is added to the console's download list. If the game is found locally, the app uses that instead of providing a download link. If not found, it falls back to the existing download link behavior.

## Implementation Summary

### 1. New Service: `IGameSearchService`
**File**: `src/application/include/gth/application/storage/IGameSearchService.h`

Provides game search capabilities:
- `searchByTitle()` - Find games by partial title match (case-insensitive)
- `searchByExactTitle()` - Exact title match
- `searchByPlatformAndRegion()` - Filter by platform/region
- `extractTitleFromUrl()` - Extract game title from CDN URLs (PSN format support)

**Result Type**: `GameSearchResult` containing:
- `game` - The matched Game entity
- `variant` - The GameVariant (platform/region)
- `version` - The GameVersion (with isCurrent flag)
- `fullPath` - Full file path to the version folder on storage

### 2. Implementation: `GameSearchService`
**Files**:
- `src/application/src/GameSearchService.h` (header)
- `src/application/src/GameSearchService.cpp` (implementation)

Features:
- Searches database for matching games by title
- Returns the most specific match (shortest title containing search term)
- Builds full file paths using Storage + GameVariant + GameVersion paths
- Extracts titles from URLs by parsing path segments
- Prioritizes Active status games with current versions

### 3. Enhanced `CdnUrlService`
**Files**:
- `src/application/include/gth/application/ftp/CdnUrlService.h`
- `src/application/src/CdnUrlService.cpp`

Changes:
- Accepts optional `IGameSearchService` dependency
- In `captureUrl()`: automatically searches for matching game
- If found: sets `localPath` and `markText` (prefix with "Local: ")
- If not found: leaves `localPath` empty, falls back to download link

### 4. ServiceContainer Integration
**Files**:
- `src/infrastructure/include/gth/infrastructure/ServiceContainer.h`
- `src/infrastructure/src/ServiceContainer.cpp`

Changes:
- Added `GameSearchService` member
- Initialized with game/variant/version/storage repositories
- Passes shared_ptr to CdnUrlService for automatic search

### 5. UI Enhancement
**File**: `src/app/src/FtpView.cpp`

Changes:
- Added "Source" column (column 3) in CDN URL list
- Shows "Local" when `localPath` is populated
- Shows "Download" when `localPath` is empty
- Adjusted column widths to accommodate new column
- Existing columns:
  - Column 0: URL
  - Column 1: Mark (shows "Local: [game name]" for local games)
  - Column 2: Local Path (shows full file path when local)
  - Column 3: Source (NEW - "Local" or "Download")
  - Column 4: Captured (date)

## Data Flow

### When URL is Captured (HTTP Proxy)
```
1. Console requests game file → Proxy intercepts URL
2. CdnUrlService.captureUrl() called with URL
3. GameSearchService.extractTitleFromUrl() parses game name from URL
4. searchByTitle() queries database for matching game
5. If found: 
   - Set CdnUrl.localPath = full path to game folder
   - Set CdnUrl.markText = "Local: [game title]"
6. If not found:
   - Leave localPath empty
   - CdnUrl ready for download link
7. UI displays "Local" or "Download" in Source column
```

### Manual URL Entry
```
1. User enters URL manually via text field
2. Same auto-search flow as above
3. UI shows whether local game was found
```

### Using the Game
```
User can:
- Copy local path directly for local transfer
- Or copy URL for download if no local version found
- Double-click in list to copy URL/path to clipboard
```

## Configuration

No configuration needed - the feature is automatic and enabled by default.

### Behavior
- **Automatic**: No UI prompts or manual input required
- **Non-invasive**: Existing download link workflow unchanged
- **Safe**: Never modifies or deletes existing games
- **Smart**: Uses best-match algorithm for title extraction

## Technical Details

### URL Title Extraction
Current implementation:
- Extracts path segment before file extension
- Removes hyphens/underscores (treats as spaces)
- Removes trailing numbers/hashes
- Example: `https://[...]/game-title-2024-v1.00/manifest.json` → "game title"

Can be enhanced for:
- PSN platform-specific URLs
- Region-specific CDN URLs
- Version number parsing
- Game ID to title mapping

### Database Queries
- `IGameRepository.getAllGames()` - Get all games (lightweight)
- `IGameVariantRepository.getGameVariants(gameId)` - Variants for a game
- `IGameVersionRepository.getGameVariantVersions(variantId)` - Versions for variant
- `IStorageRepository.getAllStorages()` - Storage mount paths

All queries use existing database abstractions - no new SQL needed.

### Error Handling
- Missing storage: Version not added to results
- No active version: Variant skipped
- No matching game: Falls back to download link
- Null/empty inputs: Safely ignored

## Testing Scenarios

### Scenario 1: Game Found Locally
1. Local storage has: D:\Games\PS5\Spider-Man-2\v1.00\
2. Console requests: https://psn-cdn.com/games/spiderman-2-v1.00/manifest.zip
3. Expected: 
   - URL captured with localPath = D:\Games\PS5\Spider-Man-2\v1.00\
   - Mark = "Local: Spider-Man 2"
   - Source column shows "Local"

### Scenario 2: Game Not Found
1. Local storage: empty
2. Console requests: https://psn-cdn.com/games/new-game-v1.00/manifest.zip
3. Expected:
   - URL captured with empty localPath
   - Source column shows "Download"

### Scenario 3: Multiple Variants
1. Local storage has same game in USA and EU regions
2. Console requests game
3. Expected:
   - Finds first active variant with current version
   - Returns that path
   - (Future: could be enhanced to region-match)

## Future Enhancements

1. **Region Matching**: Prefer region-matching variant when multiple exist
2. **Version Matching**: Extract version from URL and match specific version
3. **Title Indexing**: Cache common game titles for faster searches
4. **Platform Detection**: Auto-detect PS5/PS4/Xbox from URL structure
5. **User Override**: Allow manual path selection if search fails
6. **Batch Import**: Scan URLs to auto-populate local paths for multiple games
7. **Smart Fallback**: If exact match fails, show list of close matches

## Files Modified/Created

### Created
- `src/application/include/gth/application/storage/IGameSearchService.h`
- `src/application/src/GameSearchService.h`
- `src/application/src/GameSearchService.cpp`
- `FEATURE_LOCAL_GAME_SEARCH.md` (this file)

### Modified
- `src/application/include/gth/application/ftp/CdnUrlService.h` - Added dependency injection
- `src/application/src/CdnUrlService.cpp` - Added auto-search logic
- `src/infrastructure/include/gth/infrastructure/ServiceContainer.h` - Added service registration
- `src/infrastructure/src/ServiceContainer.cpp` - Added initialization
- `src/app/src/FtpView.cpp` - Added "Source" column to UI

## Building

No additional dependencies or build configuration needed. Existing CMake setup handles new source files automatically.

```bash
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Debug
```

## Notes for Future Development

1. **Thread Safety**: URL capture happens on proxy thread - mutex in CdnUrlService handles this
2. **Performance**: Search runs on UI thread via CdnUrlService - consider async variant later
3. **Repositories**: New service depends on standard core repositories - no new DB schema needed
4. **Path Building**: Uses standard std::filesystem - Windows compatible
