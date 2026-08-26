set(_STDC_PLUGIN_METADATA_GENERATOR
    "${CMAKE_CURRENT_LIST_DIR}/commands/GeneratePluginMetadata.cmake"
)

# Embeds an IID and optional JSON metadata file into a plugin target.
#
# stdc_add_plugin_metadata(TARGET <target> IID <iid> [METADATA <metadata-json>])
function(stdc_add_plugin_metadata)
    set(_options)
    set(_one_value_args TARGET IID METADATA)
    set(_multi_value_args)
    cmake_parse_arguments(_arg
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN}
    )

    if(_arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "stdc_add_plugin_metadata: unrecognized arguments: ${_arg_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT DEFINED _arg_TARGET OR _arg_TARGET STREQUAL "")
        message(FATAL_ERROR "stdc_add_plugin_metadata: TARGET is required")
    endif()
    if(NOT TARGET ${_arg_TARGET})
        message(FATAL_ERROR "stdc_add_plugin_metadata: '${_arg_TARGET}' is not a target")
    endif()
    if(NOT DEFINED _arg_IID OR _arg_IID STREQUAL "")
        message(FATAL_ERROR "stdc_add_plugin_metadata: IID is required")
    endif()

    get_target_property(_target_type ${_arg_TARGET} TYPE)
    if(NOT _target_type STREQUAL "SHARED_LIBRARY" AND
       NOT _target_type STREQUAL "MODULE_LIBRARY")
        message(FATAL_ERROR
            "stdc_add_plugin_metadata: '${_arg_TARGET}' is not a shared or module library"
        )
    endif()

    set(_metadata_json)
    if(DEFINED _arg_METADATA AND NOT _arg_METADATA STREQUAL "")
        get_filename_component(_metadata_json "${_arg_METADATA}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        if(NOT EXISTS "${_metadata_json}")
            message(FATAL_ERROR
                "stdc_add_plugin_metadata: metadata file does not exist: ${_metadata_json}"
            )
        endif()
    endif()

    get_target_property(_existing_iid ${_arg_TARGET} STDC_PLUGIN_IID)
    if(NOT _existing_iid STREQUAL "_existing_iid-NOTFOUND")
        message(FATAL_ERROR
            "stdc_add_plugin_metadata: '${_arg_TARGET}' already has IID: ${_existing_iid}"
        )
    endif()

    set(_metadata_cpp
        "${CMAKE_CURRENT_BINARY_DIR}/${_arg_TARGET}_plugin_metadata.cpp"
    )
    add_custom_command(
        OUTPUT "${_metadata_cpp}"
        COMMAND "${CMAKE_COMMAND}"
        "-DPLUGIN_IID:STRING=${_arg_IID}"
        "-DMETADATA_FILE:FILEPATH=${_metadata_json}"
        "-DOUTPUT_FILE:FILEPATH=${_metadata_cpp}"
        -P "${_STDC_PLUGIN_METADATA_GENERATOR}"
        DEPENDS
        ${_metadata_json}
        "${_STDC_PLUGIN_METADATA_GENERATOR}"
        COMMENT "Generating embedded metadata for ${_arg_TARGET}"
        VERBATIM
    )
    set_source_files_properties("${_metadata_cpp}" PROPERTIES GENERATED TRUE)
    target_sources(${_arg_TARGET} PRIVATE "${_metadata_cpp}")

    if(APPLE AND _target_type STREQUAL "MODULE_LIBRARY")
        # CMake gives loadable modules a .so suffix on macOS. Use the native dynamic library
        # suffix so PluginFactory recognizes the generated plugin.
        set_target_properties(${_arg_TARGET} PROPERTIES SUFFIX ".dylib")
    endif()

    set_target_properties(${_arg_TARGET} PROPERTIES
        STDC_PLUGIN_IID "${_arg_IID}"
        STDC_PLUGIN_METADATA_FILE "${_metadata_json}"
    )
endfunction()
