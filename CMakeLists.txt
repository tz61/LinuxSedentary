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
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets DBus)
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

# Windows, tray, notifications and idle detection.
add_library(sedentary_gui STATIC
    src/app.cpp src/app.h
    src/idle_monitor.cpp src/idle_monitor.h
    src/notifier.cpp src/notifier.h
    src/icons.cpp src/icons.h
    src/break_window.cpp src/break_window.h
    src/main_window.cpp src/main_window.h
    src/settings_dialog.cpp src/settings_dialog.h
    src/control_service.cpp src/control_service.h
)
target_link_libraries(sedentary_gui PUBLIC
    sedentary_core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::DBus
)

# Resources go into the executables: a static library would drop them.
add_executable(${APP_ID} src/main.cpp data/resources.qrc)
target_link_libraries(${APP_ID} PRIVATE sedentary_gui)

install(TARGETS ${APP_ID} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The launcher points at the absolute binary path so it works even when
# ~/.local/bin is not on the PATH of the desktop session.
set(APP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/${APP_ID}")
configure_file(data/${APP_ID}.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/${APP_ID}.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

if(SEDENTARY_BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test)
    if(TARGET Qt${QT_VERSION_MAJOR}::Test)
        enable_testing()
        foreach(test scheduler history)
            add_executable(test_${test} tests/test_${test}.cpp)
            target_link_libraries(test_${test} PRIVATE sedentary_core Qt${QT_VERSION_MAJOR}::Test)
            add_test(NAME ${test} COMMAND test_${test})
        endforeach()

        # Renders every window and tray icon state offscreen (PNG files in
        # <build>/snapshots), which doubles as a GUI smoke test.
        add_executable(gui_snapshot tests/gui_snapshot.cpp data/resources.qrc)
        target_link_libraries(gui_snapshot PRIVATE sedentary_gui)
        add_test(NAME gui_snapshot COMMAND gui_snapshot ${CMAKE_CURRENT_BINARY_DIR}/snapshots)
        set_tests_properties(gui_snapshot PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        message(STATUS "Qt Test not found: unit tests disabled")
    endif()
endif()
