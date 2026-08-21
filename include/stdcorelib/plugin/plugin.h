// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGIN_H
#define STDCORELIB_PLUGIN_PLUGIN_H

#include <stdcorelib/support/json.h>
#include <stdcorelib/support/staticregistry.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc::plugin {

    /// \addtogroup plugin
    /// @{

    /// Base class for all plugins.
    ///
    /// An interface and nothing else. What a plugin is, and where it was found, belongs to the
    /// \c PluginLoader that produced it, the same way a QObject knows nothing of the QPluginLoader
    /// it came out of.
    class Plugin {
    public:
        virtual ~Plugin() = default;
    };

    /// A plugin linked into the program rather than found on disk.
    ///
    /// It carries the same metadata a plugin.json would, since nothing else about it differs.
    /// The metadata comes through a function so that a \c StaticPlugin stays constant
    /// initializable, which is what lets one register itself before main runs.
    class StaticPlugin {
    public:
        using PluginInstanceFunction = Plugin *(*) ();
        using MetadataFunction = json::Value (*)();

        constexpr StaticPlugin(PluginInstanceFunction i, MetadataFunction m)
            : instance(i), metadata(m) {
        }

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

    /// The process-wide registry of plugins linked into the program.
    using StaticPluginRegistry = StaticRegistry<StaticPlugin>;

}

#if !defined(STDC_PLUGIN_LIBRARY)
extern template class STDC_PLUGIN_EXPORT stdc::StaticRegistry<stdc::plugin::StaticPlugin>;
#endif

/// The symbol a plugin library exports, as a string, for whoever has to resolve it.
#define STDC_PLUGIN_INSTANCE_SYMBOL "stdc_plugin_instance"

/// Exports \a PLUGIN_NAME from a shared library. The plugin.json beside it says the rest.
#define STDC_EXPORT_PLUGIN(PLUGIN_NAME)                                                            \
    extern "C" STDC_DECL_EXPORT stdc::plugin::Plugin *stdc_plugin_instance() {                     \
        static PLUGIN_NAME _instance;                                                              \
        return &_instance;                                                                         \
    }

/// Registers \a PLUGIN_NAME into \a PLUGIN_SET at startup.
///
/// \a METADATA is an expression yielding the \c stdc::json::Value that a plugin.json would have
/// held. It is evaluated the first time the metadata is asked for, not during registration.
#define STDC_EXPORT_STATIC_PLUGIN(PLUGIN_NAME, PLUGIN_SET, METADATA)                               \
    namespace {                                                                                    \
        stdc::plugin::StaticPluginRegistry::AddFactory                                             \
            PLUGIN_NAME##_initializer(PLUGIN_SET, "", []() -> stdc::plugin::StaticPlugin {         \
                return stdc::plugin::StaticPlugin(                                                 \
                    []() -> stdc::plugin::Plugin * {                                               \
                        static PLUGIN_NAME _instance;                                              \
                        return &_instance;                                                         \
                    },                                                                             \
                    []() -> stdc::json::Value { return (METADATA); });                             \
            });                                                                                    \
    }

#endif // STDCORELIB_PLUGIN_PLUGIN_H
