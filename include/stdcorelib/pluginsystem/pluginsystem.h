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
#include <stdcorelib/pluginsystem/pluginsettings.h>
#include <stdcorelib/pluginsystem/pluginspec.h>

namespace stdc::pluginsystem {

    /// \addtogroup plugin
    /// @{

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

        /// Which plugin settings source to access.
        enum SettingsScope {
            /// System-wide settings that override plugin metadata defaults.
            Global,
            /// Per-user settings that override global settings.
            Local,
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
        /// This invalidates previously returned specs for unloaded plugins. Calls after
        /// loadPlugins() starts have no effect.
        void setPluginPaths(array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths() const;

        /// Replaces enabled-state overrides in \a scope before loadPlugins() starts.
        ///
        /// Local settings override global settings, which override plugin metadata. Calls after
        /// loadPlugins() starts have no effect.
        void setPluginSettings(SettingsScope scope, PluginSettings settings);

        /// Returns the settings stored in \a scope.
        PluginSettings pluginSettings(SettingsScope scope) const;

        /// Returns every discovered spec.
        ///
        /// Before loadPlugins() starts, this scans the current search paths. The discovered set is
        /// frozen when loading starts. Replacing the paths before then invalidates pointers from
        /// earlier calls.
        std::vector<PluginSpec *> plugins() const;

        /// Loads and initializes every enabled, valid plugin once.
        ///
        /// Dependencies are loaded and initialized before their dependents. Errors remain on each
        /// PluginSpec.
        void loadPlugins();

        /// Shuts down and unloads plugins in reverse dependency order.
        ///
        /// Repeated calls and calls before loadPlugins() have no effect. The destructor calls this
        /// function automatically.
        void shutdownPlugins();

        /// Whether any plugin has an error.
        bool hasError() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginSystem)
    };

    /// @}

}

#endif // STDCORELIB_PLUGINSYSTEM_PLUGINSYSTEM_H
