cmake_minimum_required(VERSION 3.16)
project(SecureVault VERSION 3.0.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)   # application icon for SecureVaultGui
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SV_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SV_BUILD_TESTS "Build the regression tests" ON)

# One version constant for the CLI banner and the GUI About page.
add_compile_definitions(SV_VERSION="${PROJECT_VERSION}")

function(sv_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(SV_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# ---- securevault_core: Qt-free, console-free ------------------------------------------------
add_library(securevault_core STATIC
    src/Crypto.cpp
    src/EncryptionEngine.cpp
    src/FileVault.cpp
    src/Vault.cpp
    src/PasswordChecker.cpp
    src/PasswordGenerator.cpp
    src/AuditLogger.cpp
    src/ClipboardManager.cpp
    src/SecurityManager.cpp
    src/Utils.cpp
    src/InstanceLock.cpp
    src/Session.cpp
    src/AuthService.cpp
    src/VaultAnalyzer.cpp
)
target_include_directories(securevault_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(securevault_core PUBLIC bcrypt user32)
else()
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(securevault_core PUBLIC OpenSSL::Crypto Threads::Threads)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(securevault_core PUBLIC stdc++fs)
endif()
sv_warnings(securevault_core)

# ---- SecureVault: the CLI, a thin front-end over the core -----------------------------------
add_executable(SecureVault cli/main.cpp cli/Console.cpp)
target_include_directories(SecureVault PRIVATE cli)
target_link_libraries(SecureVault PRIVATE securevault_core)
sv_warnings(SecureVault)

# ---- tests -----------------------------------------------------------------------------------
if(SV_BUILD_TESTS)
    enable_testing()
    add_executable(backend_tests tests/backend_regression_tests.cpp)
    target_link_libraries(backend_tests PRIVATE securevault_core)
    sv_warnings(backend_tests)
    add_test(NAME backend_regression COMMAND backend_tests)

    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE securevault_core)
    sv_warnings(core_tests)
    add_test(NAME core_tests COMMAND core_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND AND NOT WIN32)
        add_test(NAME cli_smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.py $<TARGET_FILE:SecureVault>)
    endif()
endif()

# ---- SecureVaultGui: built only when Qt 6.5+ with Quick Controls is present ---------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gui/CMakeLists.txt)
    find_package(Qt6 6.5 COMPONENTS Quick QuickControls2 QUIET)
    if(Qt6_FOUND)
        add_subdirectory(gui)
    else()
        message(STATUS "Qt 6.5 Quick not found: SecureVaultGui will not be built (the CLI is unaffected)")
    endif()
endif()
