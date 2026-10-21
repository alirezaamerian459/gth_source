# SQLite is distributed as a plain-C amalgamation (no CMake support of its
# own), fetched directly from the official sqlite.org download - not a
# third-party GitHub mirror - and built here as its own tiny static library.
include(FetchContent)
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
)
FetchContent_MakeAvailable(sqlite3)

add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_ENABLE_FTS5
)

add_library(gth_database STATIC
    src/Database.cpp
    src/Migrator.cpp
    src/StorageRepository.cpp
    src/GameRepository.cpp
    src/GameVariantRepository.cpp
    src/GameVersionRepository.cpp
    src/GameFileRepository.cpp
    src/ScanJobRepository.cpp
    src/UpdateInfoRepository.cpp
    src/UpdateJobRepository.cpp
    src/UpdateHistoryRepository.cpp
    src/ConsoleRepository.cpp
    src/TransferJobRepository.cpp
    src/TransferItemRepository.cpp
    src/RemoteNodeRepository.cpp
    src/NasDeviceRepository.cpp
    src/DiskRepository.cpp
    src/HealthSnapshotRepository.cpp
    src/TemperatureSnapshotRepository.cpp
    src/FtpConnectionRepository.cpp
    src/CdnMirrorRepository.cpp
    src/CdnUrlRepository.cpp
    src/AppSettingsRepository.cpp
)

target_include_directories(gth_database PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_database PUBLIC gth_core sqlite3)
target_compile_features(gth_database PUBLIC cxx_std_20)
