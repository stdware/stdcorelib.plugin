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

set(_plugin_dir ${CMAKE_CURRENT_SOURCE_DIR}/plugins/test_pluginloader_plugin)

add_library(test_pluginloader_plugin MODULE ${_plugin_dir}/test_pluginloader_plugin.cpp)
target_link_libraries(test_pluginloader_plugin PRIVATE stdcorelib::plugin)
set_target_properties(test_pluginloader_plugin PROPERTIES PREFIX "")
stdc_add_plugin_manifest(test_pluginloader_plugin
    ${_plugin_dir}/test_pluginloader_plugin.json
)

add_library(test_pluginloader_library MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/test_pluginloader_library.cpp
)
set_target_properties(test_pluginloader_library PROPERTIES PREFIX "")

set(_pluginsystem_plugin_dir ${CMAKE_CURRENT_SOURCE_DIR}/plugins/test_pluginsystem_plugin)
add_library(test_pluginsystem_plugin MODULE
    ${_pluginsystem_plugin_dir}/test_pluginsystem_plugin.cpp
)
target_link_libraries(test_pluginsystem_plugin PRIVATE stdcorelib::plugin)
set_target_properties(test_pluginsystem_plugin PROPERTIES PREFIX "")
stdc_add_plugin_manifest(test_pluginsystem_plugin
    ${_pluginsystem_plugin_dir}/test_pluginsystem_plugin.json
)

add_library(test_pluginsystem_failing_plugin MODULE
    ${_pluginsystem_plugin_dir}/test_pluginsystem_failing_plugin.cpp
)
target_link_libraries(test_pluginsystem_failing_plugin PRIVATE stdcorelib::plugin)
set_target_properties(test_pluginsystem_failing_plugin PROPERTIES PREFIX "")
stdc_add_plugin_manifest(test_pluginsystem_failing_plugin
    ${_pluginsystem_plugin_dir}/test_pluginsystem_plugin.json
)

foreach(_name IN ITEMS provider consumer)
    set(_target test_pluginsystem_lifecycle_${_name})
    add_library(${_target} MODULE
        ${_pluginsystem_plugin_dir}/test_pluginsystem_lifecycle_plugin.cpp
    )
    target_link_libraries(${_target} PRIVATE stdcorelib::plugin)
    target_compile_definitions(${_target} PRIVATE
        TEST_PLUGINSYSTEM_LIFECYCLE_NAME="${_name}"
    )
    set_target_properties(${_target} PROPERTIES PREFIX "")
    if(APPLE)
        set_target_properties(${_target} PROPERTIES SUFFIX ".dylib")
    endif()
endforeach()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TEST_PLUGINLOADER_PLUGIN_PATH="$<TARGET_FILE:test_pluginloader_plugin>"
    TEST_PLUGINLOADER_LIBRARY_PATH="$<TARGET_FILE:test_pluginloader_library>"
    TEST_PLUGINLOADER_MANIFEST_PATH="${_plugin_dir}/test_pluginloader_plugin.json"
    TEST_PLUGINSYSTEM_FAILING_PLUGIN_PATH="$<TARGET_FILE:test_pluginsystem_failing_plugin>"
    TEST_PLUGINSYSTEM_LIFECYCLE_CONSUMER_PATH="$<TARGET_FILE:test_pluginsystem_lifecycle_consumer>"
    TEST_PLUGINSYSTEM_LIFECYCLE_PROVIDER_PATH="$<TARGET_FILE:test_pluginsystem_lifecycle_provider>"
    TEST_PLUGINSYSTEM_PLUGIN_PATH="$<TARGET_FILE:test_pluginsystem_plugin>"
)
add_dependencies(${PROJECT_NAME}
    test_pluginloader_library
    test_pluginloader_plugin
    test_pluginsystem_failing_plugin
    test_pluginsystem_lifecycle_consumer
    test_pluginsystem_lifecycle_provider
    test_pluginsystem_plugin
)

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

# The default 1500-second timeout turns a missing runtime DLL or a deadlock into a CI job that
# appears to run forever.
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120)
