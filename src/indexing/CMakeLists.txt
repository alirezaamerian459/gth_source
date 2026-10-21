add_library(gth_indexing STATIC
    src/FilesystemStorageScanner.cpp
    src/PkgFileParser.cpp
    src/EnhancedStorageScanner.cpp
)

target_include_directories(gth_indexing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_indexing PUBLIC gth_core)
target_compile_features(gth_indexing PUBLIC cxx_std_20)
