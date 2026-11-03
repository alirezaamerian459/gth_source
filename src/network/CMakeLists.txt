add_library(gth_network STATIC
    src/Win32NetworkInterfaceDetector.cpp
    src/TcpReachabilityChecker.cpp
    src/CentralServerListener.cpp
    src/TcpRemoteReportSender.cpp
    src/IcmpHostPinger.cpp
    src/LocalGameServer.cpp
    src/OnlineTitleLookupService.cpp
    src/SonyUpdateService.cpp
    src/SonyLinkImport.cpp
    src/SonyPieceDownloader.cpp
    src/PsPricesClient.cpp
    src/WindowsFirewallService.cpp
    src/TelegramBackupTransport.cpp
)

target_include_directories(gth_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_network PUBLIC gth_core gth_application gth_logging CURL::libcurl spdlog::spdlog)
target_compile_features(gth_network PUBLIC cxx_std_20)
