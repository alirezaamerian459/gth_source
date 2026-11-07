include(FetchContent)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
)
FetchContent_MakeAvailable(googletest)

add_executable(gth_unit_tests
    test_storage_repository.cpp
    test_storage_categories.cpp
    test_storage_service.cpp
    test_archive_path_resolver.cpp
    test_game_indexer_service.cpp
    test_version_comparer.cpp
    test_filesystem_storage_scanner.cpp
    test_update_detector_service.cpp
    test_update_job_service.cpp
    test_local_metadata_source.cpp
    test_update_scheduler_service.cpp
    test_download_manager.cpp
    test_file_verifier.cpp
    test_archive_updater.cpp
    test_update_manager_service.cpp
    test_network_interface_detector.cpp
    test_console_manager_service.cpp
    test_tcp_reachability_checker.cpp
    test_transfer_queue_service.cpp
    test_local_file_transfer_provider.cpp
    test_report_service.cpp
    test_enterprise.cpp
    test_ftp_connection_repository.cpp
    test_windows_credential_store.cpp
    test_ftp_services.cpp
    test_winsock_ftp_server.cpp
    test_ftp_server_service.cpp
    test_admin_auth_service.cpp
    test_pkg_file_parser.cpp
    test_enhanced_storage_scanner.cpp
    test_online_title_lookup_service.cpp
    test_ps_prices_client.cpp
    test_sony_update_service.cpp
    test_sony_link_import.cpp
    test_update_check_service.cpp
    test_package_sync_service.cpp
    test_sony_automation.cpp
    test_download_history.cpp
    test_sidebar_navigation.cpp
    test_sidebar_tabs.cpp
    test_ftp_layout.cpp
    test_windows_firewall_service.cpp
    test_physical_disks.cpp
    test_smart_health.cpp
    test_patch_auto_click_logic.cpp
    test_winsock_caching_proxy_logic.cpp
    test_local_game_server.cpp
    test_local_file_index_service.cpp
    test_backup_service.cpp
)

target_link_libraries(gth_unit_tests PRIVATE
    gth_core
    gth_application
    gth_database
    gth_indexing
    gth_updates
    gth_verification
    gth_network
    gth_transfer
    gth_credentials
    gth_ftp
    gth_ftpserver
    gth_captcha
    gth_storage_detection
    gth_acceleratorserver
    GTest::gtest_main
)

target_include_directories(gth_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app/src)
target_compile_features(gth_unit_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(gth_unit_tests)
