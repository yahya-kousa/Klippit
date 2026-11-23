cmake_minimum_required(VERSION 3.21)
project(klippit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)
qt_standard_project_setup()
qt_add_executable(klippit
    src/main.cpp
    src/Editor.h src/Editor.cpp
    src/ClipQueue.h src/ClipQueue.cpp
    src/Timeline.h src/Timeline.cpp
    src/ClipExporter.h src/ClipExporter.cpp
    src/SaveFolders.h src/SaveFolders.cpp
)
target_link_libraries(klippit PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
set_target_properties(klippit PROPERTIES WIN32_EXECUTABLE TRUE)

option(BUILD_TESTING "Build the Klippit regression tests" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(export_trim_test tests/export_trim.cpp src/ClipExporter.cpp src/ClipExporter.h)
    target_include_directories(export_trim_test PRIVATE src)
    target_link_libraries(export_trim_test PRIVATE Qt6::Core)
    add_test(NAME lossless_trim COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_trim.py" $<TARGET_FILE:export_trim_test>)
    set_tests_properties(lossless_trim PROPERTIES TIMEOUT 90)
    add_executable(capture_start_test tests/capture_start.cpp src/recorder/Loopback.cpp)
    target_include_directories(capture_start_test PRIVATE src/recorder)
    target_link_libraries(capture_start_test PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(capture_start_test PRIVATE ole32)
    endif()
    add_test(NAME capture_start COMMAND capture_start_test)
    set_tests_properties(capture_start PROPERTIES TIMEOUT 30)
    add_executable(replay_buffer_test tests/replay_buffer.cpp src/recorder/ReplayBuffer.cpp src/ClipQueue.cpp)
    target_include_directories(replay_buffer_test PRIVATE src/recorder src)
    target_link_libraries(replay_buffer_test PRIVATE Qt6::Core)
    add_test(NAME replay_buffer COMMAND Python3::Interpreter -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_replay.py" $<TARGET_FILE:replay_buffer_test> $<TARGET_FILE:export_trim_test>)
    set_tests_properties(replay_buffer PROPERTIES TIMEOUT 60)
    add_test(NAME recording_metrics COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_recording_metrics.py")
    set_tests_properties(recording_metrics PROPERTIES TIMEOUT 30)
endif()

if(WIN32)
    target_sources(klippit PRIVATE src/ReplayTray.cpp src/ReplayTray.h src/ReplaySettings.cpp src/ReplaySettings.h)
    target_link_libraries(klippit PRIVATE user32 wtsapi32)
    add_executable(klippit-record src/recorder/main.cpp src/recorder/Loopback.cpp src/recorder/Replay.cpp src/recorder/ReplayBuffer.cpp src/ClipQueue.cpp)
    target_include_directories(klippit-record PRIVATE src)
    target_link_libraries(klippit-record PRIVATE Qt6::Core user32 ole32)

    # Source builds can run without the installer and updater SDK.
    set(KLIPPIT_VELOPACK_SDK "" CACHE PATH "Extracted Velopack SDK for Windows x64 packaging")
    if(KLIPPIT_VELOPACK_SDK)
        set(velo_lib "${KLIPPIT_VELOPACK_SDK}/lib/velopack_libc_win_x64_msvc.dll")
        target_sources(klippit PRIVATE src/AppUpdate.cpp src/AppUpdate.h)
        target_compile_definitions(klippit PRIVATE KLIPPIT_PACKAGED)
        option(KLIPPIT_LOCAL_UPDATE_TEST "Use the local Windows smoke-test feed" OFF)
        if(KLIPPIT_LOCAL_UPDATE_TEST)
            target_compile_definitions(klippit PRIVATE KLIPPIT_LOCAL_UPDATE_TEST)
        endif()
        target_include_directories(klippit PRIVATE "${KLIPPIT_VELOPACK_SDK}/include")
        target_link_libraries(klippit PRIVATE "${velo_lib}.lib")
        install(TARGETS klippit klippit-record RUNTIME DESTINATION .)
        # The SDK archive decorates the filename, but its import library requests this basename.
        install(FILES "${velo_lib}" DESTINATION . RENAME velopack_libc.dll)
        install(FILES LICENSE DESTINATION .)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
        include(InstallRequiredSystemLibraries)
    endif()
endif()
