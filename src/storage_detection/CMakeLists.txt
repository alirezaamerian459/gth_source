add_library(gth_storage_detection STATIC
    src/LocalStorageDetector.cpp
    src/WindowsStorageHealthProvider.cpp
    src/SmartHealth.cpp
)

target_include_directories(gth_storage_detection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_storage_detection PUBLIC gth_core)
target_compile_features(gth_storage_detection PUBLIC cxx_std_20)
