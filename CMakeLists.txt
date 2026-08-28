cmake_minimum_required(VERSION 3.16)

project(StardewTranslator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# QtConcurrent runs the translation job outside the UI thread. QtNetwork is
# used by TranslationProvider for OpenAI-compatible HTTP requests.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)
include(GNUInstallDirs)

set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/translationprovider.cpp
    src/translationprovider.h
    src/jsonutils.cpp
    src/jsonutils.h
    src/settingssecurity.cpp
    src/settingssecurity.h
)

add_executable(${PROJECT_NAME} WIN32 ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE crypt32)
endif()

# `cmake --install` creates a self-contained application directory. Qt's
# deployment script collects Qt plugins, Qt DLLs and the MinGW runtime instead
# of requiring users to configure PATH on the target computer.
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
qt_generate_deploy_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deployScript
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deployScript})

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# JsonUtils has no network or UI dependency, so it can be tested independently.
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        add_executable(JsonUtilsTests
            tests/jsonutils_test.cpp
            src/jsonutils.cpp
            src/jsonutils.h
            src/translationprovider.cpp
            src/translationprovider.h
            src/settingssecurity.cpp
            src/settingssecurity.h
        )
        target_include_directories(JsonUtilsTests PRIVATE src)
        target_link_libraries(JsonUtilsTests PRIVATE Qt6::Test Qt6::Network)
        if(WIN32)
            target_link_libraries(JsonUtilsTests PRIVATE crypt32)
        endif()
        add_test(NAME JsonUtilsTests COMMAND JsonUtilsTests)
    endif()
endif()
