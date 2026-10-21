add_library(gth_acceleratorserver STATIC
    src/WinsockCachingProxyServer.cpp
    src/WinsockCachingProxyServerInternal.cpp
    src/DownloadAcceleratorServerFactory.cpp
)

target_include_directories(gth_acceleratorserver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_acceleratorserver PUBLIC gth_core gth_application gth_network)
target_compile_features(gth_acceleratorserver PUBLIC cxx_std_20)
