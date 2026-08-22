set(_STDC_PLUGIN_METADATA_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/commands/GeneratePluginMetadata.cmake"
)

# Embeds a JSON metadata file into a plugin target.
#
# stdc_add_plugin_metadata(<target> <metadata-json>)
function(stdc_add_plugin_metadata _target _metadata_json)
    if(NOT TARGET ${_target})
        message(FATAL_ERROR "stdc_add_plugin_metadata: '${_target}' is not a target")
    endif()

    get_filename_component(_metadata_json "${_metadata_json}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(NOT EXISTS "${_metadata_json}")
        message(FATAL_ERROR
            "stdc_add_plugin_metadata: metadata file does not exist: ${_metadata_json}"
        )
    endif()

    get_target_property(_existing_metadata ${_target} STDC_PLUGIN_METADATA_FILE)

    if(_existing_metadata)
        message(FATAL_ERROR
            "stdc_add_plugin_metadata: '${_target}' already has metadata: ${_existing_metadata}"
        )
    endif()

    set(_metadata_cpp
        "${CMAKE_CURRENT_BINARY_DIR}/${_target}_plugin_metadata.cpp"
    )
    add_custom_command(
        OUTPUT "${_metadata_cpp}"
        COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE:FILEPATH=${_metadata_json}"
        "-DOUTPUT_FILE:FILEPATH=${_metadata_cpp}"
        -P "${_STDC_PLUGIN_METADATA_GENERATOR}"
        DEPENDS
        "${_metadata_json}"
        "${_STDC_PLUGIN_METADATA_GENERATOR}"
        COMMENT "Generating embedded metadata for ${_target}"
        VERBATIM
    )
    set_source_files_properties("${_metadata_cpp}" PROPERTIES GENERATED TRUE)
    target_sources(${_target} PRIVATE "${_metadata_cpp}")

    get_target_property(_target_type ${_target} TYPE)

    if(APPLE AND _target_type STREQUAL "MODULE_LIBRARY")
        # CMake gives loadable modules a .so suffix on macOS. Use the native dynamic library
        # suffix so PluginFactory recognizes the generated plugin.
        set_target_properties(${_target} PROPERTIES SUFFIX ".dylib")
    endif()

    set_target_properties(${_target} PROPERTIES
        STDC_PLUGIN_METADATA_FILE "${_metadata_json}"
    )
endfunction()
