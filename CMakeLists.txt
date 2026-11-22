cmake_minimum_required(VERSION 3.21)
project(klippit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)
qt_standard_project_setup()
qt_add_executable(klippit
    src/main.cpp
    src/Editor.h src/Editor.cpp
    src/Timeline.h src/Timeline.cpp
    src/ClipExporter.h src/ClipExporter.cpp
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
    add_executable(replay_buffer_test tests/replay_buffer.cpp src/recorder/ReplayBuffer.cpp)
    target_include_directories(replay_buffer_test PRIVATE src/recorder)
    target_link_libraries(replay_buffer_test PRIVATE Qt6::Core)
    add_test(NAME replay_buffer COMMAND Python3::Interpreter -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_replay.py" $<TARGET_FILE:replay_buffer_test>)
    set_tests_properties(replay_buffer PROPERTIES TIMEOUT 60)
    add_test(NAME recording_metrics COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_recording_metrics.py")
    set_tests_properties(recording_metrics PROPERTIES TIMEOUT 30)
endif()

if(WIN32)
    add_executable(klippit-record src/recorder/main.cpp src/recorder/Replay.cpp src/recorder/ReplayBuffer.cpp)
    target_link_libraries(klippit-record PRIVATE Qt6::Core user32)
endif()
