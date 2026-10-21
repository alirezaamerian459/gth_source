add_library(gth_core STATIC
    src/StorageType.cpp
    src/StorageStatus.cpp
    src/Guid.cpp
    src/Clock.cpp
    src/GameVariantStatus.cpp
    src/ScanJobStatus.cpp
    src/DefaultArchivePathResolver.cpp
    src/VersionComparer.cpp
    src/UpdateStatus.cpp
    src/UpdateJobStatus.cpp
    src/NetworkInterfaceInfo.cpp
    src/Console.cpp
    src/TransferStatus.cpp
    src/NasStatus.cpp
    src/DiskStatus.cpp
    src/FtpConnection.cpp
)

target_include_directories(gth_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gth_core PUBLIC cxx_std_20)
