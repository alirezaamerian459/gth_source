add_library(gth_captcha STATIC
    CaptchaContextService.cpp
    SonyLinkCopyWatcher.cpp
    MouseAutomationLock.cpp
    BrowserWindow.cpp
    MouseInput.cpp
    PatchAutoClickLogic.cpp
    PatchAutoClicker.cpp
)

target_link_libraries(gth_captcha PUBLIC spdlog::spdlog gth_core)
target_include_directories(gth_captcha PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/)
target_compile_features(gth_captcha PUBLIC cxx_std_20)
