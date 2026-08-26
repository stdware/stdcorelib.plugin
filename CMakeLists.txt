cmake_minimum_required(VERSION 3.16)

project(stdcorelib-plugin
    VERSION 0.1.0.0
    LANGUAGES CXX
    DESCRIPTION "Plugin management module"
    HOMEPAGE_URL "https://github.com/stdware/stdcorelib.plugin"
)

# ----------------------------------
# Build Options
# ----------------------------------
# With neither STDC_PLUGIN_BUILD_STATIC nor STDC_PLUGIN_BUILD_SHARED set, the library follows
# BUILD_SHARED_LIBS, which leaves it static when that is not set either.
option(STDC_PLUGIN_BUILD_STATIC "Build static library" OFF)
option(STDC_PLUGIN_BUILD_SHARED "Build shared library" OFF)
option(STDC_PLUGIN_BUILD_TESTS "Build test cases" OFF)
option(STDC_PLUGIN_ENABLE_EXCEPTIONS "Build the library with exceptions" ON)
option(STDC_PLUGIN_INSTALL "Install library" ON)

# ----------------------------------
# CMake Settings
# ----------------------------------
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/out/bin)
endif()

if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/out/lib)
endif()

if(NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/out/lib)
endif()

if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /manifest:no")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")

    if(NOT DEFINED CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX "d")
    endif()
elseif(MINGW)
    set(CMAKE_STATIC_LIBRARY_PREFIX "")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif()

if(STDC_PLUGIN_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif()

# ----------------------------------
# Project Variables
# ----------------------------------
set(STDC_PLUGIN_VERSION ${PROJECT_VERSION})
set(STDC_PLUGIN_INSTALL_NAME ${PROJECT_NAME})

# ----------------------------------
# Dependencies
# ----------------------------------
find_package(stdcorelib REQUIRED)

# ----------------------------------
# Main Project
# ----------------------------------
include(cmake/AddPluginMetadata.cmake)

file(GLOB _src
    src/plugin/*.cpp
    src/pluginsystem/*.cpp
)
list(FILTER _src EXCLUDE REGEX "pluginloader_(win|linux|mac)\\.cpp$")

if(WIN32)
    list(APPEND _src src/plugin/pluginloader_win.cpp)
elseif(APPLE)
    list(APPEND _src src/plugin/pluginloader_mac.cpp)
else()
    list(APPEND _src src/plugin/pluginloader_linux.cpp)
endif()

if(STDC_PLUGIN_BUILD_STATIC OR(NOT STDC_PLUGIN_BUILD_SHARED AND NOT BUILD_SHARED_LIBS))
    add_library(${PROJECT_NAME} STATIC)
    target_compile_definitions(${PROJECT_NAME} PUBLIC STDC_PLUGIN_STATIC)
else()
    add_library(${PROJECT_NAME} SHARED)
endif()

# Exported as stdcorelib::plugin, so that a consumer of both spells them the same way and the
# hyphen in the project name stays out of the code.
set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME plugin)
add_library(stdcorelib::plugin ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE ${_src})
target_compile_definitions(${PROJECT_NAME} PRIVATE STDC_PLUGIN_LIBRARY)

if(STDC_PLUGIN_ENABLE_EXCEPTIONS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC STDC_PLUGIN_EXCEPTIONS=1)
else()
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /EHs-c- /wd4530)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions)
    endif()
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC stdcorelib::stdcorelib)

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/plugin
    src/pluginsystem
    include
    include/stdcorelib/plugin
    include/stdcorelib/pluginsystem
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Add platform specific
if(WIN32)
    stdc_add_win_rc(${PROJECT_NAME}
        COPYRIGHT "Copyright (C) 2022-present Stdware Collections"
    )
endif()

if(NOT WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,-z,defs)
endif()

if(STDC_PLUGIN_BUILD_TESTS)
    enable_testing()

    add_subdirectory(tests)
endif()

if(STDC_PLUGIN_INSTALL)
    install(TARGETS ${PROJECT_NAME}
        EXPORT ${STDC_PLUGIN_INSTALL_NAME}Targets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" OPTIONAL
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" OPTIONAL
    )
    if(WIN32)
        set(_debug_symbols_dir "${CMAKE_INSTALL_BINDIR}")
    else()
        set(_debug_symbols_dir "${CMAKE_INSTALL_LIBDIR}")
    endif()
    stdc_add_install_pdb_rule(${PROJECT_NAME} DESTINATION "${_debug_symbols_dir}")

    set(_install_dir ${CMAKE_INSTALL_LIBDIR}/cmake/${STDC_PLUGIN_INSTALL_NAME})

    stdc_add_install_package_rule(
        NAME ${STDC_PLUGIN_INSTALL_NAME}
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        INSTALL_DIR ${_install_dir}
        CONFIG_TEMPLATE ${CMAKE_CURRENT_LIST_DIR}/${STDC_PLUGIN_INSTALL_NAME}Config.cmake.in
        NAMESPACE stdcorelib::
        EXPORT ${STDC_PLUGIN_INSTALL_NAME}Targets
        WRITE_CONFIG_OPTIONS NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
        DESTINATION ${_install_dir}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
endif()
