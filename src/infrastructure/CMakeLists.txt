add_library(gth_infrastructure STATIC
    src/ServiceContainer.cpp
    src/LocalGameBackupSource.cpp
)

target_include_directories(gth_infrastructure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_infrastructure PUBLIC
    gth_application
    gth_database
    gth_storage_detection
    gth_indexing
    gth_updates
    gth_verification
    gth_network
    gth_transfer
    gth_credentials
    gth_ftp
    gth_ftpserver
    gth_acceleratorserver
)

target_compile_features(gth_infrastructure PUBLIC cxx_std_20)
