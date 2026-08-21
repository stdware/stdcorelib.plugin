// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H

#include <filesystem>
#include <memory>
#include <string>

#include <stdcorelib/support/versionnumber.h>

#include <stdcorelib/plugin/pluginloader.h>

namespace stdc::pluginsystem {

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

        /// Creates a spec for \a loader, which must outlive this object.
        explicit PluginSpec(plugin::PluginLoader &loader);
        ~PluginSpec();

        PluginSpec(PluginSpec &&RHS) noexcept;
        PluginSpec &operator=(PluginSpec &&RHS) noexcept;

    public:
        State state() const;
        bool hasError() const;
        const std::string &errorMessage() const;

        /// The stable identifier used by dependencies and settings.
        const std::string &id() const;

        /// The display name, which does not have to be unique.
        const std::string &name() const;

        const VersionNumber &version() const;
        const VersionNumber &compatVersion() const;

        /// The shared library path, or empty for a static or runtime plugin.
        const std::filesystem::path &filePath() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginSpec)
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSPEC_H
