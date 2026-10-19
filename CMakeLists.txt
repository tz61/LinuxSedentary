cmake_minimum_required(VERSION 3.16)

project(SedentaryReminder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SEDENTARY_BUILD_TESTS "Build the unit tests" ON)

# Qt 6 is preferred; Qt 5.15 (what most LTS distributions ship) works too.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
if(QT_VERSION_MAJOR EQUAL 5 AND Qt5Core_VERSION VERSION_LESS 5.15)
    message(FATAL_ERROR "Qt 5.15 or newer is required (found ${Qt5Core_VERSION})")
endif()
message(STATUS "Building against Qt ${QT_VERSION}")

include(GNUInstallDirs)

# A Qt outside the system library path (e.g. ~/Qt/6.x/gcc_64) must stay
# findable after installation.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(APP_ID sedentary-reminder)

# Timer logic, statistics and settings: no widgets, so the tests can use it.
add_library(sedentary_core STATIC
    src/scheduler.cpp src/scheduler.h
    src/history.cpp src/history.h
    src/settings.cpp src/settings.h
    src/autostart.cpp src/autostart.h
    src/exercises.cpp src/exercises.h
    src/format.cpp src/format.h
)
target_include_directories(sedentary_core PUBLIC src)
target_link_libraries(sedentary_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(sedentary_core PUBLIC
    APP_ID="${APP_ID}"
    APP_VERSION="${PROJECT_VERSION}"
)

if(SEDENTARY_BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test)
    if(TARGET Qt${QT_VERSION_MAJOR}::Test)
        enable_testing()
        foreach(test scheduler history)
            add_executable(test_${test} tests/test_${test}.cpp)
            target_link_libraries(test_${test} PRIVATE sedentary_core Qt${QT_VERSION_MAJOR}::Test)
            add_test(NAME ${test} COMMAND test_${test})
        endforeach()
    else()
        message(STATUS "Qt Test not found: unit tests disabled")
    endif()
endif()
