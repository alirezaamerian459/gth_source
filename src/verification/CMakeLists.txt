add_library(gth_verification STATIC
    src/Sha256.cpp
    src/FileVerifier.cpp
)

target_include_directories(gth_verification PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_verification PUBLIC gth_core)
target_compile_features(gth_verification PUBLIC cxx_std_20)
