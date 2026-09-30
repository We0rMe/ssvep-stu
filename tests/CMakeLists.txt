cmake_minimum_required(VERSION 3.16)
project(StudentChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
get_filename_component(STUDENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${STUDENT_ROOT}/ui")
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL Concurrent Test)
find_package(OpenGL REQUIRED)
if(NOT EXISTS "${ORIGINAL_ROOT}/src/SSVEPStimulusWidget.cpp")
 message(FATAL_ERROR "Set ORIGINAL_ROOT for read-only comparison with the original renderer")
endif()
file(GLOB SOURCES "${STUDENT_ROOT}/src/*.cpp")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reference")
configure_file("${ORIGINAL_ROOT}/src/SSVEPStimulusWidget.cpp" "${CMAKE_CURRENT_BINARY_DIR}/reference/SSVEPStimulusWidget.cpp" COPYONLY)
configure_file("${ORIGINAL_ROOT}/include/SSVEPStimulusWidget.h" "${CMAKE_CURRENT_BINARY_DIR}/reference/SSVEPStimulusWidget.h" COPYONLY)
list(REMOVE_ITEM SOURCES "${STUDENT_ROOT}/src/main.cpp" "${STUDENT_ROOT}/src/EEGThread.cpp" "${STUDENT_ROOT}/src/wrapper.cpp")
add_executable(student_checks StudentChecks.cpp TestAccess.cpp ${SOURCES}
 "${STUDENT_ROOT}/res.qrc" "${STUDENT_ROOT}/ui/Impedance.ui"
 "${STUDENT_ROOT}/include/MainWindow.h" "${STUDENT_ROOT}/include/ImpedancePanel.h"
 "${STUDENT_ROOT}/include/Session.h" "${STUDENT_ROOT}/include/StimulusWindow.h"
 "${STUDENT_ROOT}/include/SSVEPOfflineThread.h" "${STUDENT_ROOT}/include/SSVEPOnlineThread.h"
 "${STUDENT_ROOT}/include/EEGDataSaver.h"
 "${CMAKE_CURRENT_BINARY_DIR}/reference/SSVEPStimulusWidget.cpp" "${CMAKE_CURRENT_BINARY_DIR}/reference/SSVEPStimulusWidget.h")
target_include_directories(student_checks PRIVATE "${STUDENT_ROOT}/include" "${CMAKE_CURRENT_BINARY_DIR}/reference" "${ORIGINAL_ROOT}/include" "${STUDENT_ROOT}/third_party/DSPFilters/include")
target_compile_definitions(student_checks PRIVATE SSVEP_WITH_EEGO=0)
target_compile_options(student_checks PRIVATE /bigobj /utf-8 /EHsc)
target_link_libraries(student_checks PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL Qt5::Concurrent Qt5::Test OpenGL::GL "${STUDENT_ROOT}/build-hw/Release/DSPFilters.lib")
