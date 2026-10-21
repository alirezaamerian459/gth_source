# PKG File Support Implementation

## Overview

This document describes the implementation of `.pkg` (PlayStation game package) file support in the Game Transfer Hub application. The system now recognizes and indexes both traditional directory structures and individual `.pkg` files.

## What Changed

### New Components

1. **PkgFileParser** (`src/indexing/include/gth/indexing/PkgFileParser.h` / `.cpp`)
   - Parses PlayStation `.pkg` file headers
   - Extracts metadata: title, titleId, contentId, version, region, platform, size
   - Handles PS4, PS3, and PSVita title ID formats
   - Automatically detects region based on title ID codes

2. **EnhancedStorageScanner** (`src/indexing/include/gth/indexing/EnhancedStorageScanner.h` / `.cpp`)
   - Replaces `FilesystemStorageScanner` in the service container
   - Performs dual scanning:
     - Traditional: `Platform/Title/Region/Version/...files` directory structure
     - New: Recursive search for `.pkg` files anywhere in storage
   - Intelligently merges results from both scanning modes
   - Converts PKG metadata to standard `ScannedVariant` format

### Modified Files

1. **`src/indexing/CMakeLists.txt`**
   - Added `PkgFileParser.cpp` and `EnhancedStorageScanner.cpp` to library

2. **`src/infrastructure/include/gth/infrastructure/ServiceContainer.h`**
   - Changed include from `FilesystemStorageScanner.h` to `EnhancedStorageScanner.h`
   - Changed member type from `FilesystemStorageScanner` to `EnhancedStorageScanner`

## How It Works

### Scanning Process

When you click "Scan All Storages" in the Games tab:

1. **Directory Scan** (original behavior)
   - Looks for: `Platform/Title/Region/Version/...files`
   - Extracts: title, platform, region from folder names
   - Builds: games list from existing directory structure

2. **PKG Scan** (new)
   - Recursively scans all directories for `.pkg` files
   - Parses each `.pkg` file's header
   - Extracts: title, titleId, region, platform, version, size
   - Groups by game variant (platform/region)

3. **Merge**
   - If a PKG matches an existing variant → adds as new version
   - If a PKG is new → creates new game variant
   - Combines results from both scan modes

### PKG File Format

PlayStation `.pkg` files have a standardized header:

- **Magic bytes (0x0-0x3)**: `0x7F`, 'P', 'K', 'G'
- **Title ID (0x30-0x39, 9 bytes)**: Format varies by platform
  - **PS4**: `CUSA#####` (Retail), `CUSB#####` (Test), etc.
  - **PS3**: `NPUA#####`, `NPEA#####`, `NPJA#####`
  - **PSVita**: `PPKA#####`, `NPPA#####`
- **Content ID (0x39-0x60, 48 bytes)**: Human-readable game name
- **Size**: File size in bytes

### Title ID to Metadata Conversion

The system automatically extracts:

| Component | Position | PS4 Example | Meaning |
|-----------|----------|-------------|---------|
| Platform Code | 0-2 | `CUS` | PS4 Commercial |
| Region | 2-3 | `A` (Americas), `E` (Europe), `J` (Japan) | Geographic region |
| Retail Code | 3 | `A` (Retail), `B` (Test) | Distribution type |
| Game Number | 4-8 | `12345` | Unique identifier |

#### Region Code Mapping

- `A` = Americas (USA, Canada, Mexico)
- `E` = Europe (EU regions)
- `J` = Japan
- `K` = Korea
- `H` = Hong Kong
- `T` = Taiwan
- `C` = China
- `R` = Russia
- `S` = Southeast Asia
- `O` = Other

### Example Scan Result

```
Input: /games/some-title-v1.00.pkg (PS4 game, USA region)

Extracted:
- Title: "some-title-v1.00" (from filename) or ContentId
- Platform: "PS4" (from titleId starting with "CUS")
- Region: "USA" (from titleId's region character)
- Version: "1.00" (default or extracted from PKG)
- Size: ~50GB (file size in bytes)

Output:
Game: "some-title"
  └─ Variant: PS4/USA
       └─ Version: 1.00 (50GB) ← points to /games/some-title-v1.00.pkg
```

## Metadata Resolution

The system uses this priority for determining game title:

1. **ContentId field** from PKG header (human-readable game name)
2. **TitleId** if ContentId is empty or invalid
3. **Filename** as fallback
4. **Generated name** (Game_TITLEID) if all else fails

## Platform Detection

Automatically determines platform based on title ID prefix:

| Prefix | Platform |
|--------|----------|
| `CUS*`, `BUS*` | PS4 |
| `NPUA*`, `NPEA*`, `NPJA*` | PS3 |
| `PPKA*`, `NPPA*` | PSVita |
| Other | PS4 (default) |

## Building

The new components are automatically included when building:

```bash
cd C:\Users\DrGame\Desktop\local
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

CMake automatically:
- Compiles `PkgFileParser.cpp` and `EnhancedStorageScanner.cpp`
- Links against `gth_core` and `gth_indexing`
- Compiles with C++20 standard

## Testing

To test the PKG support:

1. **Prepare test .pkg files**:
   - Place `.pkg` files in your storage directory (any location)
   - Or organize in traditional structure: `PS4/Game Title/USA/1.00/`

2. **Scan**:
   - Open the Games tab
   - Click "Scan All Storages"
   - Wait for scan to complete

3. **Verify**:
   - Check that .pkg files are recognized
   - Verify metadata (Title, Platform, Region, Version, Size)
   - Confirm both directory and .pkg games appear

## Troubleshooting

### Games Not Found

**Problem**: `.pkg` files in storage but not showing in Games tab

**Solutions**:
1. Verify storage is added in the Storage tab
2. Check that `.pkg` files have correct magic bytes: `0x7F PKG`
3. Ensure file extension is exactly `.pkg` or `.PKG`
4. Click "Scan All Storages" to trigger indexing

### Incorrect Metadata

**Problem**: Title/Region/Version showing as "Unknown"

**Causes**:
1. PKG header is corrupted or non-standard
2. Title ID format is not recognized
3. Content ID field is empty or invalid

**Fix**:
- Try renaming file to include game name: `spiderman-2.pkg`
- Manually add to database if PKG is genuinely non-standard

### Performance

**If scanning is slow**:
1. Large number of .pkg files in deeply nested directories
2. Network/NAS storage is slow
3. File system performance issue

**Optimization**:
- IndexingScanner uses `directory_options::skip_permission_denied`
- Silently skips inaccessible files
- Short-circuits on parse errors (doesn't throw)

## Architecture

```
ServiceContainer
  └─ EnhancedStorageScanner
      ├─ FilesystemStorageScanner (directory scan)
      └─ PkgFileParser (for each .pkg found)
           └─ detectPlatform(), detectRegion(), extractVersion()
```

The scanner inherits from `core::IStorageScanner`, so it's a drop-in replacement
for the original filesystem scanner. Existing code doesn't need changes.

## Future Improvements

Possible enhancements (not implemented in this version):

1. **Version extraction from PKG**: Currently hardcoded to "1.00"
   - Could parse PKG content to find version file
   - Would require more complex PKG parsing

2. **ISO/CUE support**: Similar logic could handle PlayStation ISO files
   - Would add separate `IsoFileParser` class
   - Merge into same EnhancedStorageScanner

3. **Retro consoles**: Extend to NES/SNES/Genesis ROM detection
   - Would need format-specific parsers

4. **Metadata caching**: Cache extracted PKG metadata
   - Avoid re-parsing unchanged files on rescan
   - Would need modification timestamp tracking

## References

- PlayStation PKG File Format: https://www.psdevwiki.com/ps4/PKG_File_Format
- PS4 Content IDs: https://www.psdevwiki.com/ps4/Content_ID
