// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGIN_H
#define STDCORELIB_PLUGIN_PLUGIN_H

#include <stdcorelib/support/json.h>

#include <stdcorelib/stdc_plugin_global.h>

namespace stdc {

    /// \addtogroup plugin
    /// @{

    /// Base class for all plugins.
    ///
    /// An interface and nothing else. What a plugin is, and where it was found, belongs to the
    /// \c PluginSpec that produced it, the same way a QObject knows nothing of the QPluginLoader
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

    public:
        STDC_PLUGIN_EXPORT static void registerStaticPlugin(const char *pluginSet,
                                                            StaticPlugin plugin);
    };

    /// @}

}

/// The symbol a plugin library exports, as a string, for whoever has to resolve it.
#define STDC_PLUGIN_INSTANCE_SYMBOL "stdc_plugin_instance"

/// Exports \a PLUGIN_NAME from a shared library. The plugin.json beside it says the rest.
#define STDC_EXPORT_PLUGIN(PLUGIN_NAME)                                                            \
    extern "C" STDC_DECL_EXPORT stdc::Plugin *stdc_plugin_instance() {                             \
        static PLUGIN_NAME _instance;                                                              \
        return &_instance;                                                                         \
    }

/// Registers \a PLUGIN_NAME into \a PLUGIN_SET at startup.
///
/// \a METADATA is an expression yielding the \c stdc::json::Value that a plugin.json would have
/// held. It is evaluated the first time the metadata is asked for, not during registration.
#define STDC_EXPORT_STATIC_PLUGIN(PLUGIN_NAME, PLUGIN_SET, METADATA)                               \
    namespace {                                                                                    \
        struct PLUGIN_NAME##_initializer {                                                         \
            PLUGIN_NAME##_initializer() {                                                          \
                stdc::StaticPlugin::registerStaticPlugin(                                          \
                    PLUGIN_SET, stdc::StaticPlugin(                                                \
                                    []() -> stdc::Plugin * {                                       \
                                        static PLUGIN_NAME _instance;                              \
                                        return &_instance;                                         \
                                    },                                                             \
                                    []() -> stdc::json::Value { return (METADATA); }));            \
            }                                                                                      \
        } PLUGIN_NAME##_initializer_instance;                                                      \
    }

#endif // STDCORELIB_PLUGIN_PLUGIN_H
