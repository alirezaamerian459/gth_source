add_library(gth_credentials STATIC
    src/WindowsCredentialStore.cpp
)

target_include_directories(gth_credentials PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_credentials PUBLIC gth_core)
target_compile_features(gth_credentials PUBLIC cxx_std_20)
