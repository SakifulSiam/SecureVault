# Built only when the root CMakeLists finds Qt 6.5+ (Quick, QuickControls2). Never link Qt6::Network.
if(NOT TARGET securevault_core)
    message(FATAL_ERROR "Build from the project root (the folder with include/ and src/), not from gui/. "
            "Copy gui/ into SecureVault/ and run: cmake -S . -B build")
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Concurrent)
qt_standard_project_setup()
set(SV_GUI_SOURCES main.cpp AppController.cpp)
if(WIN32)
    list(APPEND SV_GUI_SOURCES securevault.rc)   # needs enable_language(RC) in the root CMakeLists
endif()
qt_add_executable(SecureVaultGui WIN32 ${SV_GUI_SOURCES})
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(SecureVaultGui URI SecureVault.Gui VERSION 1.0 QML_FILES qml/Main.qml qml/Theme.qml)
qt_add_resources(SecureVaultGui assets PREFIX "/assets" BASE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    fonts/Inter-Regular.ttf fonts/Inter-Medium.ttf fonts/Inter-SemiBold.ttf fonts/JetBrainsMono-Regular.ttf fonts/JetBrainsMono-Medium.ttf
    icon/securevault.png
    icons/layout-grid.svg icons/key-round.svg icons/file-text.svg icons/credit-card.svg icons/star.svg
    icons/wand-sparkles.svg icons/shield-check.svg icons/activity.svg icons/settings.svg icons/lock.svg)
target_link_libraries(SecureVaultGui PRIVATE securevault_core Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent)
# FileDialog (QtQuick.Dialogs) is a QML module that ships with Qt; no extra link needed.

# Pure presentation helper, tested without Qt.
add_executable(cardbrand_tests tests/cardbrand_test.cpp)
add_test(NAME cardbrand COMMAND cardbrand_tests)
