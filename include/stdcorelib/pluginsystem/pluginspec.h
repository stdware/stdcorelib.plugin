// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/json.h>
#include <stdcorelib/support/versionnumber.h>

#include <stdcorelib/pluginsystem/plugindependency.h>

namespace stdc::pluginsystem {

    /// \addtogroup plugin
    /// @{

    class IPlugin;

    class PluginSpecData;

    /// Describes one plugin discovered by a PluginSystem.
    class STDC_PLUGIN_EXPORT PluginSpec {
    public:
        /// The metadata and lifecycle state of this plugin.
        enum State {
            /// Metadata or dependencies are invalid.
            Invalid,
            /// PluginSystem metadata has been read.
            Read,
            /// Dependencies have been resolved.
            Resolved,
            /// The plugin library has been loaded.
            Loaded,
            /// initialize() has succeeded.
            Initialized,
            /// pluginInitialized() has been called.
            Running,
            /// The plugin has been shut down.
            Stopped,
        };

        /// The current metadata and lifecycle state.
        State state() const;

        /// Whether this plugin has an error.
        bool hasError() const;

        /// The current error message, or empty when \c hasError() is false.
        const std::string &errorMessage() const;

        /// The stable identifier used by dependencies and settings.
        const std::string &id() const;

        /// The display name, which does not have to be unique.
        const std::string &name() const;

        /// The plugin version.
        const VersionNumber &version() const;

        /// The oldest plugin version with which this version is compatible.
        const VersionNumber &compatVersion() const;

        /// The dependencies declared by this plugin.
        const std::vector<PluginDependency> &dependencies() const;

        /// The complete plugin manifest.
        const json::Value &manifest() const;

        /// Whether the manifest and global settings enable this plugin before local overrides.
        bool enabledByGlobalSettings() const;

        /// The effective enabled state, frozen when loadPlugins() starts.
        bool isEnabled() const;

        /// The dynamic plugin path.
        const std::filesystem::path &filePath() const;

        /// Returns the loaded plugin instance, or null before loading and after unloading.
        ///
        /// The pointer remains valid until shutdownPlugins() unloads its library.
        IPlugin *plugin() const;

    private:
        explicit PluginSpec(PluginSpecData *data);

        PluginSpecData *_data;

        friend class PluginSpecData;

        STDC_DISABLE_COPY_MOVE(PluginSpec)
    };

    /// @}

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H
