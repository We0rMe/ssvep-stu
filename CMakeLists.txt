cmake_minimum_required(VERSION 3.16)
project(SSVEP_Student_Starter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")

option(SSVEP_WITH_EEGO "Build real eego acquisition and impedance support" OFF)
set(EEGO_SDK_ROOT "" CACHE PATH "Directory containing eemagine/sdk headers")
set(EEGO_SDK_LIB "" CACHE FILEPATH "eego-SDK import library")

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets OpenGL Concurrent)
find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
    find_path(EIGEN3_INCLUDE_DIR Eigen/Dense DOC "Path containing Eigen/Dense")
    if(NOT EIGEN3_INCLUDE_DIR)
        message(FATAL_ERROR "Install Eigen3 or set EIGEN3_INCLUDE_DIR to the directory containing Eigen/Dense")
    endif()
    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}")
endif()
find_package(OpenGL REQUIRED)

file(GLOB DSP_SOURCES CONFIGURE_DEPENDS third_party/DSPFilters/source/*.cpp)
add_library(DSPFilters STATIC ${DSP_SOURCES})
target_include_directories(DSPFilters PUBLIC third_party/DSPFilters/include)

set(APP_SOURCES
    res.qrc ui/Impedance.ui
    src/SSVEPOfflineThread.cpp src/SSVEPOnlineThread.cpp src/EEGDataSaver.cpp
    src/main.cpp src/MainWindow.cpp src/StimulusWindow.cpp src/Session.cpp
    src/ModelDecoder.cpp src/CsvWriter.cpp src/ImpedancePanel.cpp
    src/SSVEPModelTrainer.cpp src/SSVEPPreprocess.cpp src/train_trca.cpp
    src/test_trca.cpp src/filterbank.cpp src/itr.cpp)
set(APP_HEADERS
    include/SSVEPOfflineThread.h include/SSVEPOnlineThread.h include/EEGDataSaver.h
    include/MainWindow.h include/StimulusWindow.h include/Session.h
    include/ModelDecoder.h include/CsvWriter.h include/ImpedancePanel.h)
if(SSVEP_WITH_EEGO)
    if(NOT EXISTS "${EEGO_SDK_ROOT}/eemagine/sdk/factory.h" OR NOT EXISTS "${EEGO_SDK_LIB}")
        message(FATAL_ERROR "Set EEGO_SDK_ROOT and EEGO_SDK_LIB to your locally installed eego SDK")
    endif()
    list(APPEND APP_SOURCES src/EEGThread.cpp src/wrapper.cpp)
    list(APPEND APP_HEADERS include/EEGThread.h)
endif()

add_executable(ssvep_student WIN32 ${APP_SOURCES} ${APP_HEADERS})
set_target_properties(ssvep_student PROPERTIES OUTPUT_NAME ssvep_stu)
if(WIN32)
    enable_language(RC)
    configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(ssvep_student PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()
target_include_directories(ssvep_student PRIVATE include)
target_link_libraries(ssvep_student PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL Qt5::Concurrent Eigen3::Eigen OpenGL::GL DSPFilters)
if(SSVEP_WITH_EEGO)
    target_include_directories(ssvep_student PRIVATE "${EEGO_SDK_ROOT}" "${EEGO_SDK_ROOT}/eemagine/sdk")
    target_link_libraries(ssvep_student PRIVATE "${EEGO_SDK_LIB}")
    target_compile_definitions(ssvep_student PRIVATE SSVEP_WITH_EEGO=1)
else()
    target_compile_definitions(ssvep_student PRIVATE SSVEP_WITH_EEGO=0)
endif()
if(MSVC)
    target_compile_options(DSPFilters PRIVATE /EHsc)
    target_compile_options(ssvep_student PRIVATE /bigobj /utf-8)
    target_compile_options(ssvep_student PRIVATE /EHsc)
endif()

