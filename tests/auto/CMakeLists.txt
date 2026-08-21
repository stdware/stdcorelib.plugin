project(test_auto LANGUAGES CXX)

find_package(Boost CONFIG COMPONENTS unit_test_framework)

# Fatal rather than a warning and a return. Somebody who set STDC_PLUGIN_BUILD_TESTS=ON and
# got a green build with no tests in it has been told nothing.
if(NOT Boost_FOUND)
    message(FATAL_ERROR
        "STDC_PLUGIN_BUILD_TESTS is on but Boost.Test was not found. Install it, or point CMake "
        "at it with -DCMAKE_PREFIX_PATH, or turn the tests off with -DSTDC_PLUGIN_BUILD_TESTS=OFF.")
endif()

file(GLOB _src *.cpp)
add_executable(${PROJECT_NAME} ${_src})

set(_plugin_dir ${CMAKE_CURRENT_SOURCE_DIR}/plugins/pluginloader_test_plugin)

add_library(pluginloader_test_plugin MODULE ${_plugin_dir}/pluginloader_test_plugin.cpp)
target_link_libraries(pluginloader_test_plugin PRIVATE stdcorelib::plugin)
set_target_properties(pluginloader_test_plugin PROPERTIES PREFIX "")

if(WIN32)
    target_sources(pluginloader_test_plugin PRIVATE ${_plugin_dir}/pluginloader_test_plugin.rc)
    set_property(SOURCE ${_plugin_dir}/pluginloader_test_plugin.rc PROPERTY OBJECT_DEPENDS
        ${_plugin_dir}/pluginloader_test_plugin.json
    )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TEST_PLUGINLOADER_PLUGIN_PATH="$<TARGET_FILE:pluginloader_test_plugin>"
    TEST_PLUGINLOADER_METADATA_PATH="${_plugin_dir}/pluginloader_test_plugin.json"
)
add_dependencies(${PROJECT_NAME} pluginloader_test_plugin)

target_link_libraries(${PROJECT_NAME} PRIVATE stdcorelib::plugin)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::unit_test_framework)

# Some tests reach into private headers (src/plugin/*_p.h) to cover internals the public API
# cannot get at on its own.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/plugin)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
