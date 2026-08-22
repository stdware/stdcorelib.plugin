set(_STDC_PLUGIN_MANIFEST_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/commands/GeneratePluginManifest.cmake"
)

# Embeds a JSON manifest file into a plugin target.
#
# stdc_add_plugin_manifest(<target> <manifest-json>)
function(stdc_add_plugin_manifest _target _manifest_json)
    if(NOT TARGET ${_target})
        message(FATAL_ERROR "stdc_add_plugin_manifest: '${_target}' is not a target")
    endif()

    get_filename_component(_manifest_json "${_manifest_json}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(NOT EXISTS "${_manifest_json}")
        message(FATAL_ERROR
            "stdc_add_plugin_manifest: manifest file does not exist: ${_manifest_json}"
        )
    endif()

    get_target_property(_existing_manifest ${_target} STDC_PLUGIN_MANIFEST_FILE)

    if(_existing_manifest)
        message(FATAL_ERROR
            "stdc_add_plugin_manifest: '${_target}' already has a manifest: ${_existing_manifest}"
        )
    endif()

    set(_manifest_cpp
        "${CMAKE_CURRENT_BINARY_DIR}/${_target}_plugin_manifest.cpp"
    )
    add_custom_command(
        OUTPUT "${_manifest_cpp}"
        COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE:FILEPATH=${_manifest_json}"
        "-DOUTPUT_FILE:FILEPATH=${_manifest_cpp}"
        -P "${_STDC_PLUGIN_MANIFEST_GENERATOR}"
        DEPENDS
        "${_manifest_json}"
        "${_STDC_PLUGIN_MANIFEST_GENERATOR}"
        COMMENT "Generating embedded manifest for ${_target}"
        VERBATIM
    )
    set_source_files_properties("${_manifest_cpp}" PROPERTIES GENERATED TRUE)
    target_sources(${_target} PRIVATE "${_manifest_cpp}")

    get_target_property(_target_type ${_target} TYPE)

    if(APPLE AND _target_type STREQUAL "MODULE_LIBRARY")
        # CMake gives loadable modules a .so suffix on macOS. Use the native dynamic library
        # suffix so PluginFactory recognizes the generated plugin.
        set_target_properties(${_target} PROPERTIES SUFFIX ".dylib")
    endif()

    set_target_properties(${_target} PROPERTIES
        STDC_PLUGIN_MANIFEST_FILE "${_manifest_json}"
    )
endfunction()
