// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
#define STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H

#include <filesystem>
#include <memory>
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

        explicit PluginSystem(PluginLayout layout = Flat);
        ~PluginSystem();

        PluginSystem(PluginSystem &&RHS) noexcept;
        PluginSystem &operator=(PluginSystem &&RHS) noexcept;

    public:
        PluginLayout pluginLayout() const;

        /// Adds a filesystem search path. Set every path before first calling plugins().
        void addPluginPath(const std::filesystem::path &path);
        void setPluginPaths(array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths() const;

        /// Adds every static PluginSystem plugin registered under the fixed PluginSystem IID.
        void addStaticPlugins();

        /// Adds a live PluginSystem plugin. Ownership remains with the caller.
        void addRuntimePlugin(IPlugin *plugin, const json::Value &metadata);

        /// Returns every discovered spec, preserving each pointer for this system's lifetime.
        std::vector<PluginSpec *> plugins() const;

        /// The one IID accepted by this system.
        static std::string_view pluginIID();

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginSystem)
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
