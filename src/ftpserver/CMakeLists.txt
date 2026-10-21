add_library(gth_ftpserver STATIC
    src/WinsockFtpServer.cpp
)

target_include_directories(gth_ftpserver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_ftpserver PUBLIC gth_core)
target_compile_features(gth_ftpserver PUBLIC cxx_std_20)
