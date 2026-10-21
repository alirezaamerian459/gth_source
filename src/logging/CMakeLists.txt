include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

add_library(gth_logging STATIC
    src/Logging.cpp
)

target_include_directories(gth_logging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_logging PUBLIC spdlog::spdlog)
target_compile_features(gth_logging PUBLIC cxx_std_20)
