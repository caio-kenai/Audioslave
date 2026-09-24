cmake_minimum_required(VERSION 3.22)

# Single-config generators (Ninja, NMake) default to Release; multi-config
# generators (Visual Studio) ignore this and use --config. Must be set before
# project(), which is where MSVC would otherwise pick Debug.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

# Static CRT everywhere (JUCE modules included), so the binaries have no
# runtime dependency on the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Audioslave
    VERSION 2.0.0
    DESCRIPTION "Monitors Windows audio devices and prevents applications from using exclusive audio mode"
    HOMEPAGE_URL "https://github.com/caio-kenai/Audioslave"
    LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Audioslave is a Windows application (it relies on Core Audio, the SCM and COM).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------
# JUCE (git submodule pinned to a release tag; see MIGRATION_PLAN.md §12)
# ---------------------------------------------------------------------------
set(AUDIOSLAVE_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH
    "JUCE checkout to build against (defaults to the pinned submodule)")
if(NOT EXISTS "${AUDIOSLAVE_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE was not found in ${AUDIOSLAVE_JUCE_DIR}.\n"
        "Run:  git submodule update --init --recursive\n"
        "or pass -DAUDIOSLAVE_JUCE_DIR=<path to a JUCE 9 checkout>.")
endif()
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
add_subdirectory("${AUDIOSLAVE_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Generated headers (single source of truth for the version)
# ---------------------------------------------------------------------------
set(AUDIOSLAVE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/common/AudioslaveVersion.h.in "${AUDIOSLAVE_GENERATED_DIR}/AudioslaveVersion.h" @ONLY)

# Every executable lands in <build>/bin for every configuration; $<1:...>
# stops multi-config generators from appending a per-configuration folder.
set(AUDIOSLAVE_BIN_DIR "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# audioslave_juce: the JUCE modules, compiled exactly once.
# JUCE modules are INTERFACE libraries that must be linked PRIVATE; this
# static library re-exports their include paths and definitions so every
# Audioslave target can use JUCE without compiling the modules again.
# ---------------------------------------------------------------------------
add_library(audioslave_juce STATIC)
target_link_libraries(audioslave_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(audioslave_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_ASIO=0
        JUCE_USE_WINDOWS_MEDIA_FORMAT=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODAL_LOOPS_PERMITTED=0)
target_include_directories(audioslave_juce
    INTERFACE $<TARGET_PROPERTY:audioslave_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(audioslave_juce
    INTERFACE $<TARGET_PROPERTY:audioslave_juce,COMPILE_DEFINITIONS>)
set_target_properties(audioslave_juce PROPERTIES FOLDER "JUCE")

# Warnings and Windows definitions for Audioslave's own code only (the JUCE
# module sources configure the Windows headers themselves).
add_library(audioslave_options INTERFACE)
target_compile_definitions(audioslave_options INTERFACE
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
    WIN32_LEAN_AND_MEAN NOMINMAX STRICT UNICODE _UNICODE)
if(MSVC)
    target_compile_options(audioslave_options INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /utf-8 /permissive- /Zc:__cplusplus /EHsc /sdl /MP>)
endif()

# ---------------------------------------------------------------------------
# audioslave_core: everything except the entry points and the UI.
# ---------------------------------------------------------------------------
add_library(audioslave_core STATIC
    src/common/Branding.h
    src/common/Strings.h
    src/platform/windows/WinCommon.h
    src/platform/windows/WinHandles.h
    src/platform/windows/WinError.h
    src/platform/windows/WinError.cpp
    src/platform/windows/ScopedComInit.h
    src/platform/windows/Paths.h
    src/platform/windows/Paths.cpp
    src/platform/windows/EventLog.h
    src/platform/windows/EventLog.cpp
    src/platform/windows/Elevation.h
    src/platform/windows/Elevation.cpp
    src/platform/windows/SessionInstance.h
    src/platform/windows/SessionInstance.cpp
    src/logging/Logger.h
    src/logging/Logger.cpp
    src/config/Configuration.h
    src/config/Configuration.cpp
    src/audio/models/AudioEndpoint.h
    src/audio/models/AudioFormat.h
    src/audio/models/AudioFormat.cpp
    src/audio/models/DeviceChange.h
    src/audio/models/ResultCode.h
    src/audio/AudioInterfaces.h
    src/audio/windows/ComHelpers.h
    src/audio/windows/ComHelpers.cpp
    src/audio/windows/WindowsAudioEndpointEnumerator.h
    src/audio/windows/WindowsAudioEndpointEnumerator.cpp
    src/audio/windows/WindowsAudioDeviceWatcher.h
    src/audio/windows/WindowsAudioDeviceWatcher.cpp
    src/audio/windows/WindowsExclusiveModePolicy.h
    src/audio/windows/WindowsExclusiveModePolicy.cpp
    src/audio/windows/WindowsFormatSupport.h
    src/audio/windows/WindowsFormatSupport.cpp
    src/audio/windows/WindowsAudioFormatPolicy.h
    src/audio/windows/WindowsAudioFormatPolicy.cpp
    src/audio/juce/JuceAudioDeviceInventory.h
    src/audio/juce/JuceAudioDeviceInventory.cpp
    src/audio/juce/JuceExclusiveModeProbe.h
    src/audio/juce/JuceExclusiveModeProbe.cpp
    src/core/ExclusiveModePolicy.h
    src/core/ExclusiveModePolicy.cpp
    src/core/FormatPolicy.h
    src/core/FormatPolicy.cpp
    src/core/EndpointMemory.h
    src/core/EndpointMemory.cpp
    src/core/EngineStatus.h
    src/core/WatchdogEngine.h
    src/core/WatchdogEngine.cpp
    src/ipc/Protocol.h
    src/ipc/Protocol.cpp
    src/ipc/PipeServer.h
    src/ipc/PipeServer.cpp
    src/ipc/ControlClient.h
    src/ipc/ControlClient.cpp
    src/service/ServiceHost.h
    src/service/ServiceHost.cpp
    src/service/WindowsService.h
    src/service/WindowsService.cpp
    src/service/ServiceController.h
    src/service/ServiceController.cpp
)
target_include_directories(audioslave_core PUBLIC src "${AUDIOSLAVE_GENERATED_DIR}")
target_link_libraries(audioslave_core
    PUBLIC
        audioslave_juce
        audioslave_options
        ole32 oleaut32 propsys advapi32 user32 shell32 shlwapi)
target_compile_features(audioslave_core PUBLIC cxx_std_20)
set_target_properties(audioslave_core PROPERTIES FOLDER "Audioslave")

function(audioslave_configure_executable target)
    target_link_libraries(${target} PRIVATE audioslave_core)
    target_include_directories(${target} PRIVATE resources "${AUDIOSLAVE_GENERATED_DIR}")
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<1:${AUDIOSLAVE_BIN_DIR}>"
        FOLDER "Audioslave")
endfunction()

set(AUDIOSLAVE_COMPANY "caio-kenai")

# ---------------------------------------------------------------------------
# Audioslave.exe: the single executable - system-tray application (no
# arguments), Windows service (--service) and command-line tools.
# ---------------------------------------------------------------------------
juce_add_binary_data(AudioslaveAssets
    NAMESPACE AudioslaveAssets
    HEADER_NAME AudioslaveAssets.h
    SOURCES
        assets/tray/normal-16.png assets/tray/normal-20.png assets/tray/normal-24.png
        assets/tray/normal-32.png assets/tray/normal-40.png assets/tray/normal-48.png
        assets/tray/normal-64.png assets/tray/normal-256.png
        assets/tray/paused-16.png assets/tray/paused-20.png assets/tray/paused-24.png
        assets/tray/paused-32.png assets/tray/paused-40.png assets/tray/paused-48.png
        assets/tray/paused-64.png assets/tray/paused-256.png)
set_target_properties(AudioslaveAssets PROPERTIES FOLDER "Audioslave")

juce_add_gui_app(Audioslave
    PRODUCT_NAME "Audioslave"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${AUDIOSLAVE_COMPANY}")
target_sources(Audioslave PRIVATE
    src/app/AudioslaveApplication.h
    src/app/AudioslaveApplication.cpp
    src/app/TrayController.h
    src/app/TrayController.cpp
    src/app/TrayIcon.h
    src/app/TrayIcon.cpp
    src/app/TrayMenu.h
    src/app/TrayMenu.cpp
    src/app/StatusWindow.h
    src/app/StatusWindow.cpp
    src/app/Theme.h
    src/app/Theme.cpp
    src/app/Dialog.h
    src/app/Dialog.cpp
    src/app/Main.cpp
    src/cli/Commands.h
    src/cli/Commands.cpp
    src/cli/Diagnostics.h
    src/cli/Diagnostics.cpp
    resources/Audioslave.manifest)
target_compile_definitions(Audioslave PRIVATE JUCE_USER_DEFINED_RC_FILE="Audioslave.rc")
target_link_libraries(Audioslave PRIVATE AudioslaveAssets)
audioslave_configure_executable(Audioslave)

# ---------------------------------------------------------------------------
# Tests (juce::UnitTest)
# ---------------------------------------------------------------------------
enable_testing()
juce_add_console_app(audioslave_tests
    PRODUCT_NAME "audioslave_tests"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${AUDIOSLAVE_COMPANY}")
target_sources(audioslave_tests PRIVATE
    tests/TestMain.cpp
    tests/Mocks.h
    tests/ConfigurationTests.cpp
    tests/LoggerTests.cpp
    tests/ExclusiveModePolicyTests.cpp
    tests/FormatPolicyTests.cpp
    tests/EndpointMemoryTests.cpp
    tests/WatchdogEngineTests.cpp
    tests/ProtocolTests.cpp
    tests/IpcTests.cpp
    tests/ServiceHostTests.cpp
    tests/TrayControllerTests.cpp
    tests/WindowsIntegrationTests.cpp
    src/app/TrayController.h
    src/app/TrayController.cpp)
target_compile_definitions(audioslave_tests PRIVATE JUCE_USER_DEFINED_RC_FILE="AudioslaveTests.rc")
target_include_directories(audioslave_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
audioslave_configure_executable(audioslave_tests)
add_test(NAME audioslave_tests COMMAND audioslave_tests)

# ---------------------------------------------------------------------------
# Audioslave-Setup.exe: JUCE installer with Audioslave.exe embedded as RCDATA
# (IDR_PAYLOAD). Doubles as Uninstall.exe.
# ---------------------------------------------------------------------------
# rc.exe reads the payload through this generated include, with the absolute
# path of the freshly built executable (never a stale copy).
file(GENERATE OUTPUT "${AUDIOSLAVE_GENERATED_DIR}/payload.rc"
     CONTENT "IDR_PAYLOAD RCDATA \"$<TARGET_FILE:Audioslave>\"\n")

juce_add_gui_app(AudioslaveSetup
    PRODUCT_NAME "Audioslave-Setup"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${AUDIOSLAVE_COMPANY}")
target_sources(AudioslaveSetup PRIVATE
    src/installer/SetupMain.cpp
    src/installer/SetupOptions.h
    src/installer/SetupOptions.cpp
    src/installer/InstallerCore.h
    src/installer/InstallerCore.cpp
    src/installer/SetupWizard.h
    src/installer/SetupWizard.cpp
    src/app/Theme.h
    src/app/Theme.cpp
    src/app/Dialog.h
    src/app/Dialog.cpp
    resources/AudioslaveSetup.manifest)
target_compile_definitions(AudioslaveSetup PRIVATE JUCE_USER_DEFINED_RC_FILE="AudioslaveSetup.rc")
target_link_options(AudioslaveSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
target_link_libraries(AudioslaveSetup PRIVATE AudioslaveAssets)
audioslave_configure_executable(AudioslaveSetup)
# The setup embeds Audioslave.exe: build it first and re-run rc.exe when it changes.
add_dependencies(AudioslaveSetup Audioslave)
add_dependencies(AudioslaveSetup_rc_lib Audioslave)
get_target_property(_setup_generated AudioslaveSetup JUCE_GENERATED_SOURCES_DIRECTORY)
set_source_files_properties("${_setup_generated}/AudioslaveSetup_resources.rc" PROPERTIES
    OBJECT_DEPENDS "${AUDIOSLAVE_BIN_DIR}/Audioslave.exe")

# ---------------------------------------------------------------------------
# audioslave_screenshots (development only, not built by default): renders
# the tray menu, status window, dialogs and installer with sample data to
# PNG files (docs/screenshots) using JUCE's own renderer.
# ---------------------------------------------------------------------------
juce_add_gui_app(audioslave_screenshots
    PRODUCT_NAME "audioslave_screenshots"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${AUDIOSLAVE_COMPANY}")
set_target_properties(audioslave_screenshots PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(audioslave_screenshots PRIVATE
    tools/Screenshots.cpp
    src/app/Theme.cpp
    src/app/Dialog.h
    src/app/Dialog.cpp
    src/app/TrayController.cpp
    src/app/TrayIcon.cpp
    src/app/TrayMenu.cpp
    src/app/StatusWindow.cpp
    src/installer/SetupOptions.cpp
    src/installer/InstallerCore.cpp
    src/installer/SetupWizard.cpp)
target_compile_definitions(audioslave_screenshots PRIVATE JUCE_USER_DEFINED_RC_FILE="AudioslaveTests.rc")
target_link_libraries(audioslave_screenshots PRIVATE AudioslaveAssets)
audioslave_configure_executable(audioslave_screenshots)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Audioslave)
