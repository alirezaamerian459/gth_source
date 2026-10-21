add_library(gth_updates STATIC
    src/LocalMetadataSource.cpp
)

target_include_directories(gth_updates PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_updates PUBLIC gth_core)
target_compile_features(gth_updates PUBLIC cxx_std_20)
