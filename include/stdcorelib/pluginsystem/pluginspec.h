// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <stdcorelib/pluginsystem/plugindependency.h>

namespace stdc::pluginsystem {

    class PluginSpecData;

    /// Describes one plugin discovered by a PluginSystem.
    class STDC_PLUGIN_EXPORT PluginSpec {
    public:
        /// The metadata and lifecycle state of this plugin.
        enum State {
            /// PluginSystem metadata is invalid.
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

        State state() const;
        bool hasError() const;
        const std::string &errorMessage() const;

        /// The stable identifier used by dependencies and settings.
        const std::string &id() const;

        /// The display name, which does not have to be unique.
        const std::string &name() const;

        const VersionNumber &version() const;
        const VersionNumber &compatVersion() const;
        const std::vector<PluginDependency> &dependencies() const;

        /// The plugin shared library path.
        const std::filesystem::path &filePath() const;

    private:
        explicit PluginSpec(PluginSpecData *data);

        PluginSpecData *_data;

        friend class PluginSpecData;

        STDC_DISABLE_COPY_MOVE(PluginSpec)
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H
