// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGIN_H
#define STDCORELIB_PLUGIN_PLUGIN_H

#include <string_view>

#include <stdcorelib/support/json.h>
#include <stdcorelib/support/staticregistry.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc::plugin {

    /// \addtogroup plugin
    /// @{

    /// Base interface for plugin instances.
    class Plugin {
    public:
        virtual ~Plugin() = default;

    protected:
        Plugin() = default;

        STDC_DISABLE_MOVE(Plugin);
    };

    /// Describes a static plugin.
    ///
    /// The functions provide its instance and metadata without creating the instance during
    /// registration.
    class StaticPlugin {
    public:
        using PluginInstanceFunction = Plugin *(*) ();
        using MetadataFunction = json::Value (*)();

        /// \pre \a pluginIID refers to storage that outlives this descriptor.
        constexpr StaticPlugin(std::string_view pluginIID, PluginInstanceFunction i,
                               MetadataFunction m)
            : iid(pluginIID), instance(i), metadata(m) {
        }

        std::string_view iid;
        PluginInstanceFunction instance = nullptr;
        MetadataFunction metadata = nullptr;
    };

    /// @}

}

namespace stdc {

    template <>
    struct static_registry_traits<plugin::StaticPlugin> {
        using result_type = plugin::StaticPlugin;

        template <class V>
        static result_type construct() {
            return V();
        }
    };

}

namespace stdc::plugin {

    /// The process-wide registry of static plugins.
    using StaticPluginRegistry = StaticRegistry<StaticPlugin>;

}

STDC_DECLARE_EXPORTED_STATIC_REGISTRY(stdc::plugin::StaticPlugin, STDC_PLUGIN_EXPORT)

/// The entry-point symbol exported by a dynamic plugin.
#define STDC_PLUGIN_INSTANCE_SYMBOL "stdc_plugin_instance"

/// Places generated plugin metadata in the section inspected by \c PluginLoader.
///
/// Plugin projects should call \c stdc_add_plugin_metadata instead of using this macro directly.
#if defined(_WIN32) && defined(_MSC_VER) && !defined(__clang__)
#  pragma section(".stdcmd", read, shared)
#  define STDC_PLUGIN_METADATA_SECTION __declspec(allocate(".stdcmd"))
#elif defined(_WIN32)
#  define STDC_PLUGIN_METADATA_SECTION __attribute__((section(".stdcmd"), used))
#elif defined(__APPLE__)
#  define STDC_PLUGIN_METADATA_SECTION __attribute__((section("__TEXT,stdc_metadata"), used))
#else
#  define STDC_PLUGIN_METADATA_SECTION __attribute__((section(".stdc_metadata"), used))
#endif

/// Exports \a PLUGIN_NAME as a dynamic plugin instance.
#define STDC_EXPORT_PLUGIN(PLUGIN_NAME)                                                            \
    extern "C" STDC_DECL_EXPORT stdc::plugin::Plugin *stdc_plugin_instance() {                     \
        static PLUGIN_NAME _instance;                                                              \
        return &_instance;                                                                         \
    }

/// Registers \a PLUGIN_NAME for \a PLUGIN_IID at startup.
///
/// \a PLUGIN_METADATA yields the user metadata object. It is evaluated the first time the
/// metadata is requested.
#define STDC_EXPORT_STATIC_PLUGIN(PLUGIN_NAME, PLUGIN_IID, PLUGIN_METADATA)                        \
    namespace {                                                                                    \
        stdc::plugin::StaticPluginRegistry::AddFactory                                             \
            PLUGIN_NAME##_initializer(PLUGIN_IID, "", []() -> stdc::plugin::StaticPlugin {         \
                return stdc::plugin::StaticPlugin(                                                 \
                    PLUGIN_IID,                                                                    \
                    []() -> stdc::plugin::Plugin * {                                               \
                        static PLUGIN_NAME _instance;                                              \
                        return &_instance;                                                         \
                    },                                                                             \
                    []() -> stdc::json::Value { return (PLUGIN_METADATA); });                      \
            });                                                                                    \
    }

#endif // STDCORELIB_PLUGIN_PLUGIN_H
