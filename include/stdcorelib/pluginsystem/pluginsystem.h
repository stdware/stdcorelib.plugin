// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/array_view.h>

#include <stdcorelib/pluginsystem/iplugin.h>
#include <stdcorelib/pluginsystem/pluginspec.h>

namespace stdc::pluginsystem {

    /// Discovers and manages plugins implementing the PluginSystem lifecycle.
    class STDC_PLUGIN_EXPORT PluginSystem {
    public:
        /// How filesystem plugins are arranged below each search path.
        enum PluginLayout {
            /// Plugin libraries are directly below the search path and carry embedded manifests.
            Flat,
            /// Each child directory contains one plugin library and an external plugin.json.
            Directory,
        };

        /// Creates a system that accepts only \a iid.
        ///
        /// \pre \a iid is not empty.
        explicit PluginSystem(std::string_view iid, PluginLayout layout = Flat);
        ~PluginSystem();

        PluginSystem(PluginSystem &&RHS) noexcept;
        PluginSystem &operator=(PluginSystem &&RHS) noexcept;

    public:
        /// Returns the IID this system discovers plugins for.
        const std::string &iid() const;

        /// Returns the layout of the plugins this system discovers.
        PluginLayout pluginLayout() const;

        /// Replaces the filesystem search paths before loadPlugins() starts.
        ///
        /// Calls after loadPlugins() starts have no effect.
        void setPluginPaths(array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths() const;

        /// Returns every discovered spec, preserving each pointer for this system's lifetime.
        std::vector<PluginSpec *> plugins() const;

        /// Loads and initializes every valid plugin once. Errors remain on each PluginSpec.
        void loadPlugins();

        /// Whether any plugin has an error.
        bool hasError() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginSystem)
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
