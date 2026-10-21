add_library(gth_transfer STATIC
    src/LocalFileTransferProvider.cpp
)

target_include_directories(gth_transfer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_transfer PUBLIC gth_core)
target_compile_features(gth_transfer PUBLIC cxx_std_20)
